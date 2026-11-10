#include "p9_textures.hpp"

#include "p9_game.hpp"
#include "ps2/system.hpp"

#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// The art of players 3 and 4 from the texture pack. The pack (assets\tools\build_texture_pack.py, built from the player's disc) holds, for each new thing,
// the bytes of a disc resource it stands in for with the new texels in it. The game's own loaders do the rest: this file only lets them find the new names.
//
//   * archive_open: a table of its own in extra RAM gets an entry for each new resource name that points at the file whose place it takes; archive_index_lookup
//     consults it only when the game's own index misses, so a request for the new name reads that file from the disc. This is the only thing done at boot.
//   * on_file_read: the file just read is replaced, in the loader's buffer, by the pack's version of it (new texels and palette, same size).
//   * on_object_list_read, object_type_register, on_sprite_sheet_table_read and on_sprite_page_read: the object list, the object type table, the sprite
//     sheet table and part_page0's rectangles are copied to extra RAM with the pack's records added (dyn_play_three/four, hud_namep3/4, radar sprites).
// A copy is made again each time the game loads the table it extends. Nothing is read from the pack after the game has loaded a resource; the guest state
// only holds the pack's hash (kTexPackHash), so a netplay digest sees a peer whose pack differs.
namespace warriors::p9 {

namespace {

constexpr const char *kPackEnvironment = "WARRIORS_TEXPACK", *kDefaultPackFile = "warriors_textures.pack";
constexpr char kMagic0 = 'W', kMagic1 = 'T', kMagic2 = 'X', kMagic3 = 'P';
constexpr std::uint32_t kPackVersion = 1;
constexpr std::uint32_t kNameBytes = 32;

// Guest structures (hud.md, objects.md, gui.md, wad-contents.md).
constexpr std::uint32_t kResourceManagerPointer = 0x0050CD4C, kFileManagerPointer = 0x005E5340, kObjectTypeDbPointer = 0x00512C04;
constexpr std::uint32_t kFileManagerBufferOffset = 0x3C;
constexpr std::uint32_t kRequestNameOffset = 4;                                      // a resource request record: the name, a decimal hash
constexpr std::uint32_t kWadIndexEntry = 12, kWadIndexEntryHash = 8;                 // {offset, size, hash}; the index object is {count, entries}
constexpr std::uint32_t kObjectRecord = 36, kObjectTexture = 0x0C;                   // object list record: type hash at +0, texture dictionary hash
constexpr std::uint32_t kObjectListCount = 0x90, kObjectListRecords = 0x94;          // resource manager
constexpr std::uint32_t kSheetTableRecords = 0x9044, kSheetTableCount = 0x9048;      // resource manager; a record is {size, name hash}
constexpr std::uint32_t kSheetRecord = 8;
constexpr std::uint32_t kPageCount = 4, kPageRects = 0xC, kRectBytes = 16;     // sprite sheet page (chunk 0x4C)
constexpr std::uint32_t kTypeRecord = 0x90, kTypeName = 0x28, kTypeNameBytes = 0x1B, kTypeIndex = 0x60, kTypeHash = 0x8C, kTypeCount = 0x34BC0;
constexpr std::uint32_t kTypeCapacity = 1500;                                        // 0x34bc0 bytes of 0x90 byte records

constexpr std::uint32_t kTypeListAppend = 0x00391340, kTypeListExtend = 0x00391378;
constexpr std::uint32_t kHandleToObject = 0x00390268;                                  // handle_to_object(address of a handle)
constexpr std::uint32_t kOverheadMarkerPresent = 0x00227428;
constexpr std::uint32_t kMarkerAttachSpinning = 0x00238A88, kMarkerDetachSpinning = 0x00238AE0, kMarkerSpinningTint = 0x00238B18;
constexpr std::uint32_t kSessionIsArmiesLevel = 0x0041D110;
constexpr std::uint32_t kWorldPointer = 0x005147A4;                                   // the World object whose first word is the WAD index (archive_open)
constexpr std::uint32_t kGameEuroOffset = 0x120;                                      // W_GameState: set for the European language variants
constexpr std::uint32_t kPlayerHandle0 = 0x228;

// Names (the hash of dyn_play_one.. is the object type's model hash, the key floating_marker_setup tests).
constexpr std::uint32_t kPlayerOneName = 0x0055CE88, kPlayerTwoName = 0x0055CE98;     // "dyn_p_one", "dyn_p_two" in the game's data
constexpr std::uint32_t kMarkerHash[2][2] = {{0x071ece52, 0x6f1f7813}, {0x4fada1d6, 0xb7840c50}};   // dyn_play_three/four, the _euro variant
constexpr std::uint32_t kMarkerOneHash[2] = {0x7644ee5d, 0x1a72a32d};                // dyn_play_one, dyn_play_one_euro
constexpr const char *kNames[] = {"dyn_p_three", "dyn_p_four", "dyn_play_three", "dyn_play_four", "dyn_play_three_euro", "dyn_play_four_euro"};
constexpr std::uint32_t kRequestNames = 0, kTypeNames = 2, kEuroTypeNames = 4;        // indices into kNames
constexpr std::uint32_t kIconFadeStory = 0x50, kIconFadeArmies = 0xA0;               // rings_target_step's fade argument
constexpr std::uint32_t kIconColourFade = 0xFFFFFF00;

// Name banner records (player_panel_set_title): PLAYER 1 0x25..0x29, PLAYER 2 0x2a..0x2e, one record per language each.
constexpr std::uint32_t kBannerFirst = 0x25, kBannerLast = 0x2E, kBannerLanguages = 5, kBannerShift = 16;

// Radar sprites (part_page0 rectangles): arrows, "nP" tags and bare numbers by player.
constexpr std::uint32_t kArrow[4] = {362, 363, 371, 372}, kTag[4] = {360, 361, 375, 376}, kNumber[4] = {350, 351, 373, 374};
constexpr std::uint32_t kFirstNewTag = 373, kLastNewTag = 376;
constexpr std::uint32_t kRadarFirst = 0x15D0, kRadarStride = 0x2940;                  // HUD +0x15d0 and +0x3f10; HUD2 has the same
constexpr std::uint32_t kWidgetSprite = 200;
// A part_page0 already in memory (a state saved before the pack existed): its page chunk starts 0x204b0 bytes after its texels and 0x430 after its palette.
constexpr std::uint32_t kPageSignature = 0x00421798, kPageTexelsBefore = 0x204B0, kPagePaletteBefore = 0x430, kPageSearchFrom = 0x00100000, kPageSearchTo = 0x02000000;
constexpr std::uint32_t kPageSelfPointer = 0x14, kSearchStep = 16;

struct Resource { std::uint32_t name, dir, donor_dir, size; const std::uint8_t *data; };
struct Page { std::uint32_t sheet, old_count, count, texels_at, texel_bytes, palette_at, palette_bytes; const std::uint8_t *uv; };
struct ObjectRecord { std::uint32_t type, base, texture; };
struct TypeClone { char base[kNameBytes], name[kNameBytes]; std::uint32_t hash; };
struct SheetRecord { std::uint32_t name, donor; };

struct Pack {
    std::vector<std::uint8_t> bytes;
    std::vector<Resource> resources;
    std::vector<Page> pages;
    std::vector<ObjectRecord> records;
    std::vector<TypeClone> clones;
    std::vector<SheetRecord> sheets;
    std::uint32_t hash = 0;
    bool valid = false;
};

class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t> &bytes) : bytes_(bytes) {}
    bool ok() const { return ok_; }
    std::uint32_t u32() {
        std::uint32_t v = 0;
        take(&v, 4);
        return v;
    }
    const std::uint8_t *skip(std::size_t n) {
        if (!ok_ || pos_ + n > bytes_.size()) { ok_ = false; return nullptr; }
        const std::uint8_t *at = bytes_.data() + pos_;
        pos_ += n;
        return at;
    }
    template <class T> T record() {
        T v{};
        take(&v, sizeof v);
        return v;
    }

private:
    void take(void *to, std::size_t n) {
        const std::uint8_t *at = skip(n);
        if (at) std::memcpy(to, at, n);
    }
    const std::vector<std::uint8_t> &bytes_;
    std::size_t pos_ = 0;
    bool ok_ = true;
};

std::uint32_t fnv1a(const std::vector<std::uint8_t> &bytes) {
    constexpr std::uint32_t kOffset = 2166136261u, kPrime = 16777619u;
    std::uint32_t h = kOffset;
    for (const std::uint8_t b : bytes) h = (h ^ b) * kPrime;
    return h ? h : 1;
}

void parse(Pack &p) {
    Reader r(p.bytes);
    const std::uint8_t *magic = r.skip(4);
    if (!magic || magic[0] != kMagic0 || magic[1] != kMagic1 || magic[2] != kMagic2 || magic[3] != kMagic3 || r.u32() != kPackVersion) return;
    for (std::uint32_t n = r.u32(); n && r.ok(); --n) {
        Resource res{r.u32(), r.u32(), r.u32(), r.u32(), nullptr};
        res.data = r.skip((res.size + 3) & ~3u);
        p.resources.push_back(res);
    }
    for (std::uint32_t n = r.u32(); n && r.ok(); --n) {
        Page page{r.u32(), r.u32(), r.u32(), r.u32(), r.u32(), r.u32(), r.u32(), nullptr};
        page.uv = r.skip(page.count * kRectBytes);
        p.pages.push_back(page);
    }
    for (std::uint32_t n = r.u32(); n && r.ok(); --n) p.records.push_back(r.record<ObjectRecord>());
    for (std::uint32_t n = r.u32(); n && r.ok(); --n) p.clones.push_back(r.record<TypeClone>());
    for (std::uint32_t n = r.u32(); n && r.ok(); --n) p.sheets.push_back(r.record<SheetRecord>());
    p.valid = r.ok();
}

// The pack, read once from the file named by WARRIORS_TEXPACK (default warriors_textures.pack in the working directory). Missing or damaged: the game runs as it is.
const Pack &pack() {
    static const Pack loaded = [] {
        Pack p;
        const char *env = std::getenv(kPackEnvironment);
        const std::string path = env ? env : kDefaultPackFile;
        std::ifstream in(path, std::ios::binary);
        if (in) p.bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        parse(p);
        if (p.valid) p.hash = fnv1a(p.bytes);
        else std::fprintf(stderr, "[p9] texture pack %s %s: players 3 and 4 use the original art\n", path.c_str(), p.bytes.empty() ? "not found" : "is damaged");
        return p;
    }();
    return loaded;
}

void copy_words(std::uint32_t to, std::uint32_t from, std::uint32_t bytes) {
    for (std::uint32_t i = 0; i < bytes; i += 4) ps2::st32(to + i, ps2::ld32(from + i));
}

void store_bytes(std::uint32_t to, const std::uint8_t *from, std::uint32_t bytes) {
    for (std::uint32_t i = 0; i < bytes; i += 4) {
        std::uint32_t word = 0;
        std::memcpy(&word, from + i, 4);
        ps2::st32(to + i, word);
    }
}

std::string guest_string(std::uint32_t at, std::uint32_t limit) {
    std::string s;
    for (std::uint32_t i = 0; i < limit; ++i) {
        const char ch = static_cast<char>(ps2::ld8(at + i));
        if (!ch) break;
        s += ch;
    }
    return s;
}

bool pack_in_state() { return ps2::ld32(kTexPackHash) != 0; }
bool euro() { return game() && ps2::ld32(game() + kGameEuroOffset) != 0; }

}

namespace {

void start(const Pack &p) {
    for (std::uint32_t i = 0; i < sizeof kNames / sizeof *kNames; ++i) {
        const std::uint32_t at = kTexNames + i * kNameBytes;
        for (std::uint32_t k = 0; k < kNameBytes; ++k) ps2::st8(at + k, static_cast<std::uint8_t>(k <= std::strlen(kNames[i]) ? kNames[i][k] : 0));
    }
    ps2::st32(kTexPackHash, p.hash);
}

// The new names are not added to the game's own index: a longer index changes how long the game's linear search takes for every name it looks up and does not find,
// and a 1 or 2 player game must not notice the pack. They are entries {offset, size, hash} of a table of their own in extra RAM, which the search consults only when
// it misses (wad_alias_entry, called from archive_index_lookup's miss exit).
void add_wad_aliases(const Pack &p, std::uint32_t index) {
    if (ps2::ld32(kWadAliasCount)) return;
    const std::uint32_t count = ps2::ld32(index), entries = ps2::ld32(index + 4);
    std::uint32_t added = 0;
    for (const Resource &res : p.resources) {
        if (!res.dir || (added + 1) * kWadIndexEntry > kWadIndexBytes) continue;
        std::uint32_t donor = count;
        for (std::uint32_t i = 0; i < count && donor == count; ++i)
            if (ps2::ld32(entries + i * kWadIndexEntry + kWadIndexEntryHash) == res.donor_dir) donor = i;
        if (donor == count) continue;
        const std::uint32_t at = kWadAliasEntries + added++ * kWadIndexEntry;
        ps2::st32(at, ps2::ld32(entries + donor * kWadIndexEntry));
        ps2::st32(at + 4, ps2::ld32(entries + donor * kWadIndexEntry + 4));
        ps2::st32(at + kWadIndexEntryHash, res.dir);
    }
    ps2::st32(kWadAliasCount, added);
    P9_TRACE("[p9] texture pack %08x: %u names aliased beside the WAD index of %u", p.hash, added, count);
}

}

std::uint32_t wad_alias_entry(std::uint32_t hash) {
    for (std::uint32_t i = 0, count = ps2::ld32(kWadAliasCount); i < count; ++i)
        if (ps2::ld32(kWadAliasEntries + i * kWadIndexEntry + kWadIndexEntryHash) == hash) return kWadAliasEntries + i * kWadIndexEntry;
    return 0;
}

void wad_index_created(std::uint32_t index) {
    const Pack &p = pack();
    if (!p.valid) return;
    add_wad_aliases(p, index);
}

void file_loaded(ps2::Cpu &c) {
    const Pack &p = pack();
    if (!p.valid) return;
    const std::string name = guest_string(c.r[6].w[0] + kRequestNameOffset, kNameBytes);
    const std::uint32_t hash = static_cast<std::uint32_t>(std::strtoul(name.c_str(), nullptr, 10));
    if (pack_in_state()) ps2::st32(kTexLoading, hash);
    for (const Resource &res : p.resources) {
        if (res.name != hash || (!res.dir && !pack_in_state())) continue;
        store_bytes(ps2::ld32(ps2::ld32(kFileManagerPointer) + kFileManagerBufferOffset), res.data, res.size);
        P9_TRACE("[p9] resource %u replaced by the pack (%u bytes)", hash, res.size);
        return;
    }
}

void object_list_loaded(std::uint32_t resource_manager) {
    const Pack &p = pack();
    if (!p.valid || !pack_in_state()) return;
    const std::uint32_t count = ps2::ld32(resource_manager + kObjectListCount), records = ps2::ld32(resource_manager + kObjectListRecords);
    if (records == kObjectListCopy || (count + p.records.size()) * kObjectRecord > kObjectListBytes) return;
    copy_words(kObjectListCopy, records, count * kObjectRecord);
    std::uint32_t added = 0;
    for (const ObjectRecord &add : p.records) {
        std::uint32_t base = count;
        for (std::uint32_t i = 0; i < count && base == count; ++i)
            if (ps2::ld32(records + i * kObjectRecord) == add.base) base = i;
        if (base == count) continue;
        const std::uint32_t at = kObjectListCopy + (count + added++) * kObjectRecord;
        copy_words(at, records + base * kObjectRecord, kObjectRecord);
        ps2::st32(at, add.type);
        ps2::st32(at + kObjectTexture, add.texture);
    }
    ps2::st32(resource_manager + kObjectListRecords, kObjectListCopy);
    ps2::st32(resource_manager + kObjectListCount, count + added);
    P9_TRACE("[p9] object list: %u records, %u added", count, added);
}

void sheet_table_loaded() {
    const Pack &p = pack();
    if (!p.valid || !pack_in_state()) return;
    const std::uint32_t rm = ps2::ld32(kResourceManagerPointer);
    const std::uint32_t count = ps2::ld32(rm + kSheetTableCount), records = ps2::ld32(rm + kSheetTableRecords);
    if (records == kSheetTableCopy || (count + p.sheets.size()) * kSheetRecord > kSheetTableBytes) return;
    copy_words(kSheetTableCopy, records, count * kSheetRecord);
    std::uint32_t added = 0;
    for (const SheetRecord &add : p.sheets) {
        std::uint32_t donor = count;
        for (std::uint32_t i = 0; i < count && donor == count; ++i)
            if (ps2::ld32(records + i * kSheetRecord + 4) == add.donor) donor = i;
        if (donor == count) { added = 0; break; }
        const std::uint32_t at = kSheetTableCopy + (count + added++) * kSheetRecord;
        ps2::st32(at, ps2::ld32(records + donor * kSheetRecord));
        ps2::st32(at + 4, add.name);
    }
    ps2::st32(kTexFirstSheetRecord, added ? count : 0);
    if (!added) return;
    ps2::st32(rm + kSheetTableRecords, kSheetTableCopy);
    ps2::st32(rm + kSheetTableCount, count + added);
    P9_TRACE("[p9] sheet table: %u records, %u added", count, added);
}

namespace {

// The sheet's rectangle array is copied with the added rectangles after the original ones.
bool extend_page(const Page &ext, std::uint32_t page) {
    if (ps2::ld32(page + kPageCount) != ext.old_count || (ext.old_count + ext.count) * kRectBytes > kPageRectBytes) return false;
    copy_words(kPageRectCopy, ps2::ld32(page + kPageRects), ext.old_count * kRectBytes);
    store_bytes(kPageRectCopy + ext.old_count * kRectBytes, ext.uv, ext.count * kRectBytes);
    ps2::st32(page + kPageRects, kPageRectCopy);
    ps2::st32(page + kPageCount, ext.old_count + ext.count);
    ps2::st32(kTexPageExtended, 1);
    P9_TRACE("[p9] sheet %u: %u rectangles added", ext.sheet, ext.count);
    return true;
}

}

void page_loaded(std::uint32_t page) {
    const Pack &p = pack();
    if (!p.valid || !pack_in_state()) return;
    const std::uint32_t sheet = ps2::ld32(kTexLoading);
    ps2::st32(kTexLoading, 0);
    for (const Page &ext : p.pages)
        if (ext.sheet == sheet && extend_page(ext, page)) return;
}

void type_added(ps2::Cpu &c, std::uint32_t record) {
    const Pack &p = pack();
    if (!p.valid || !pack_in_state()) return;
    const std::string name = guest_string(record + kTypeName, kTypeNameBytes);
    const std::uint32_t db = ps2::ld32(kObjectTypeDbPointer);
    for (const TypeClone &clone : p.clones) {
        if (name != clone.base || ps2::ld32(db + kTypeCount) >= kTypeCapacity) continue;
        const std::uint32_t made = ps2::call_guest(c, kTypeListAppend, db);
        const std::uint32_t index = ps2::ld16(made + kTypeIndex);
        copy_words(made, record, kTypeRecord);
        ps2::st16(made + kTypeIndex, static_cast<std::uint16_t>(index));
        for (std::uint32_t k = 0; k < kTypeNameBytes; ++k) ps2::st8(made + kTypeName + k, static_cast<std::uint8_t>(k < std::strlen(clone.name) ? clone.name[k] : 0));
        ps2::st32(made + kTypeHash, clone.hash);
        ps2::call_guest(c, kTypeListExtend, db, made);
        P9_TRACE("[p9] object type %s added as %s (index %u)", clone.base, clone.name, index);
    }
}

namespace {

// part_page0 as the game loaded it from the original file: the page chunk is found by its signature and its own rectangle pointer, the texels and palette lie
// before it at fixed distances; they are overwritten with the pack's.
void patch_loaded_page(const Pack &p, const Page &ext) {
    for (std::uint32_t at = kPageSearchFrom; at < kPageSearchTo; at += kSearchStep) {
        if (ps2::ld32(at) != kPageSignature || ps2::ld32(at + kPageCount) != ext.old_count || ps2::ld32(at + kPageRects) != at + kPageSelfPointer) continue;
        const Resource *sheet = nullptr;
        for (const Resource &res : p.resources)
            if (res.name == ext.sheet) sheet = &res;
        if (!sheet) return;
        store_bytes(at - kPageTexelsBefore, sheet->data + ext.texels_at, ext.texel_bytes);
        store_bytes(at - kPagePaletteBefore, sheet->data + ext.palette_at, ext.palette_bytes);
        extend_page(ext, at);
        P9_TRACE("[p9] sheet %u patched in memory (page %08x)", ext.sheet, at);
        return;
    }
}

}

// The tables and the sheet the pack extends are extended in place, once, when the first extra player is added (and for a state saved with extra players before the
// pack was in use). Until then a game with one or two players has none of the pack's changes in the game's memory.
void retrofit_art(ps2::Cpu &c) {
    const Pack &p = pack();
    if (!p.valid || pack_in_state() || !ps2::ld32(kWorldPointer)) return;
    start(p);
    add_wad_aliases(p, ps2::ld32(ps2::ld32(kWorldPointer)));
    object_list_loaded(ps2::ld32(kResourceManagerPointer));
    sheet_table_loaded();
    const std::uint32_t db = ps2::ld32(kObjectTypeDbPointer);
    for (std::uint32_t i = 0, count = ps2::ld32(db + kTypeCount); i < count; ++i) type_added(c, db + i * kTypeRecord);
    for (const Page &ext : p.pages) patch_loaded_page(p, ext);
}

std::uint32_t overhead_icon_name(std::uint32_t slot) {
    if (slot == 0) return kPlayerOneName;
    if (slot < kOriginalPlayers || slot >= kMaxPlayers || !pack_in_state()) return kPlayerTwoName;
    return kTexNames + (kRequestNames + slot - kOriginalPlayers) * kNameBytes;
}

// session_add_player: a third or fourth player is about to be added, so the HUD, the banners and the markers must find the pack's records already in place.
void extra_player_joining(ps2::Cpu &c) {
    const std::uint32_t slot = ps2::ld16(c.r[4].w[0] + kPlayerCountOffset);
    if (slot >= kOriginalPlayers && slot < kMaxPlayers) retrofit_art(c);
}

// overhead_marker_show maps dyn_p_one/two to the marker types (the European ones when the game says so); the same for dyn_p_three/four.
void overhead_icon_request(ps2::Cpu &c) {
    if (!pack_in_state()) return;
    const std::string name = guest_string(c.r[5].w[0], kNameBytes);
    for (std::uint32_t slot = kOriginalPlayers; slot < kMaxPlayers; ++slot) {
        if (name != kNames[kRequestNames + slot - kOriginalPlayers]) continue;
        c.r[5].sd[0] = static_cast<std::int32_t>(kTexNames + ((euro() ? kEuroTypeNames : kTypeNames) + slot - kOriginalPlayers) * kNameBytes);
        return;
    }
}

// The markers of players 3 and 4 follow player 1's (rings_target_step attaches and removes the first two): shown while his is, with the same fade.
void sync_extra_markers(ps2::Cpu &c) {
    const std::uint32_t g = game();
    if (!g || player_count() <= kOriginalPlayers) return;
    retrofit_art(c);
    if (!pack_in_state()) return;
    const std::uint32_t variant = euro() ? 1 : 0;
    const std::uint32_t first = ps2::call_guest(c, kHandleToObject, g + kPlayerHandle0);
    const bool wanted = first && ps2::call_guest(c, kOverheadMarkerPresent, first, kMarkerOneHash[variant]) != 0;
    for (std::uint32_t slot = kOriginalPlayers; slot < player_count() && slot < kMaxPlayers; ++slot) {
        const std::uint32_t handle_at = g + kPlayerHandle0 + slot * 4;
        const std::uint32_t human = ps2::call_guest(c, kHandleToObject, handle_at);
        if (!human) continue;
        const bool shown = ps2::call_guest(c, kOverheadMarkerPresent, human, kMarkerHash[slot - kOriginalPlayers][variant]) != 0;
        const std::uint32_t handle = ps2::ld32(handle_at);
        if (wanted && !shown) {
            ps2::call_guest(c, kMarkerAttachSpinning, handle, overhead_icon_name(slot), 0);
            const bool armies = ps2::call_guest(c, kSessionIsArmiesLevel, g) != 0;
            ps2::call_guest(c, kMarkerSpinningTint, handle, armies ? kIconFadeArmies : kIconFadeStory, kIconColourFade);
        } else if (!wanted && shown) {
            ps2::call_guest(c, kMarkerDetachSpinning, handle);
        }
    }
}

bool player_marker_hash(std::uint32_t model_hash) {
    for (const auto &player : kMarkerHash)
        for (const std::uint32_t hash : player)
            if (hash == model_hash) return true;
    return false;
}

std::uint32_t banner_record(std::uint32_t record, std::uint32_t player_hud) {
    const std::uint32_t index = record >> kBannerShift, first = ps2::ld32(kTexFirstSheetRecord);
    if (index < kBannerFirst || index > kBannerLast || !first) return record;
    for (std::uint32_t panel = kOriginalPlayers; panel < kMaxPlayers; ++panel)
        if (ps2::ld32(kHud + kHudPanelPointers + panel * 4) == player_hud)
            return (first + (panel - kOriginalPlayers) * kBannerLanguages + (index - kBannerFirst) % kBannerLanguages) << kBannerShift;
    return record;
}

std::uint32_t radar_arrow_sprite(std::uint32_t slot) {
    return slot >= kOriginalPlayers && slot < kMaxPlayers && ps2::ld32(kTexPageExtended) ? kArrow[slot] : kArrow[1];
}

namespace {

int arrow_owner(std::uint32_t sprite) {
    for (int player = 0; player < kMaxPlayers; ++player)
        if (kArrow[player] == sprite) return player;
    return -1;
}

// Which player's radar a Radar object belongs to: the HUD's two and HUD2's two, in order.
int radar_viewer(std::uint32_t radar) {
    for (int hud = 0; hud < 2; ++hud) {
        const std::uint32_t base = (hud ? kHud2 : kHud) + kRadarFirst;
        if (radar >= base && radar < base + 2 * kRadarStride) return hud * kOriginalPlayers + static_cast<int>((radar - base) / kRadarStride);
    }
    return -1;
}

}

std::uint32_t arrow_player(std::uint32_t sprite) {
    const int owner = arrow_owner(sprite);
    return owner >= kOriginalPlayers ? static_cast<std::uint32_t>(owner) : (sprite == kArrow[0] ? 0 : 1);
}

bool radar_extra_tag(std::uint32_t radar, std::uint32_t widget) {
    const int owner = arrow_owner(ps2::ld32(widget + kWidgetSprite)), viewer = radar_viewer(radar);
    if (owner < 0 || viewer < 0 || (viewer < kOriginalPlayers && owner < kOriginalPlayers)) return false;
    if (owner != viewer) ps2::st32(widget + kWidgetSprite, euro() ? kNumber[owner] : kTag[owner]);
    return true;
}

bool radar_is_new_tag(std::uint32_t sprite) { return sprite >= kFirstNewTag && sprite <= kLastNewTag; }

}
