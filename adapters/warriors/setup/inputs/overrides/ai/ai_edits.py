"""Edit tables of apply_ai.py (see there for the mechanics). EDITS (unit file, old text, new text, count), FUNC_EDITS (function address, old text, new text, count: matches
inside that function only), ENTRY_HOOKS (function address, statement). The new text keeps every `c.budget` line of the old, so a 1 or 2 player game runs the original's cycles."""

HEADER = "ai_hooks.hpp"   # the hook header every edited unit includes (apply_p10.py area modules read it; apply_ai.py adds it itself)

AI = "warriors::ai::"

EDITS = []

ENTRY_HOOKS = [
    (0x001454A8, AI + "share_mug_callback(c);"),   # Pads_Update, once per frame (ai_objectives.cpp)
    (0x001454A8, AI + "bot_frame(c);"),            # the same label: the test brawlers (ai_bot.cpp, off unless PS2_AI_BOT is set)
]

FUNC_EDITS = []


def edit(address, old, new, count=1):
    FUNC_EDITS.append((address, old, new, count))


# --- Enemy strength (ai_scale.cpp) -----------------------------------------------------------------------------------------------------------------------------
GANG_SPAWNER_UPDATE, GANG_UPDATE, APPLY_PENDING_DAMAGE = 0x001681A0, 0x00166708, 0x00265F70

# A spawner's limit of living members (spawner +0x69a); r17 is the gang.
edit(GANG_SPAWNER_UPDATE, "c.r[21].sd[0] = static_cast<std::int16_t>(ld16((c.r[2].w[0] + 1690u)));",
     "c.r[21].sd[0] = %sspawner_cap(c.r[17].w[0], static_cast<std::int16_t>(ld16((c.r[2].w[0] + 1690u))));" % AI)
# The game's spawn maximum (game state +0x434), the non-police branch.
edit(GANG_UPDATE, "L_00166960:\n    c.r[4].sd[0] = static_cast<std::int16_t>(ld16((c.r[2].w[0] + 1076u)));",
     "L_00166960:\n    c.r[4].sd[0] = %sspawn_max(static_cast<std::int16_t>(ld16((c.r[2].w[0] + 1076u))));" % AI)
# The damage that is about to be taken off (record +0x118 in r19), the victim in r17 and the one who hit him in r18.
edit(APPLY_PENDING_DAMAGE, "L_0026630C:\n    c.budget -= 2;",
     "L_0026630C:\n    c.r[3].sd[0] = %senemy_damage(c.r[17].w[0], c.r[18].w[0], c.r[19].w[0], c.r[3].sd[0]);\n    c.budget -= 2;" % AI)

# --- Friendly fire (ai_rules.cpp, applied in enemy_damage): players are not friends to each other in the game's friend test, so a player's hit on a player is ordinary damage; with the switch
# on, the damage of such a hit is taken off before it is applied, which is what the AI's friendly case does (damage 0). ---------------------------------------------------------------------

# --- Weapon swap prompt (PlayerBrain_Think): with three or four players the prompt is offered to the nearest partner. ---------------------------------------------------
PLAYER_BRAIN_THINK = 0x00303260
edit(PLAYER_BRAIN_THINK, "const bool t = c.r[19].d[0] != c.r[2].d[0]; ld128((c.r[29].w[0] + 112u) & ~15u, c.r[16]);",
     "const bool t = c.r[19].sd[0] < c.r[2].sd[0]; ld128((c.r[29].w[0] + 112u) & ~15u, c.r[16]);")
edit(PLAYER_BRAIN_THINK, "    c.r[20].d[0] = c.r[2].d[0] + 0ull;\n    c.budget -= 1;\nL_00303300:",
     "    c.r[20].d[0] = %snearer_partner(c.r[18].w[0], c.r[20].w[0], c.r[2].w[0]);\n    c.budget -= 1;\n"
     "    if (c.r[19].sd[0] > 2) { set32(c.r[16], c.r[16].w[0] + 4u);  if (c.budget < 0 && preempt(c)) { c.pc = 0x003032D0u | kUnwind; return; } goto L_003032D0; }\nL_00303300:" % AI)

# --- Stationary shooters (StationaryShooterGoal_PickTarget): the random pick is among all players. -----------------------------------------------------------------
edit(0x002F0318, "set32(c.r[5], 0u + 2u);", "set32(c.r[5], %sshooter_choices());" % AI)


# --- Boss hit armour (ai_scale.cpp): a class 13 boss in the wind-up of an attack takes no reaction from a hit by a non-player (Human_ApplyPendingDamage); with three or four players
# the armour holds against players too, so a crowd cannot interrupt every swing. ---------------------------------------------------------------------------------------------
FOE_SLOT_TEST = ("    c.r[2].sd[0] = static_cast<std::int8_t>(ld8((c.r[18].w[0] + 432u)));\n    c.r[2].d[0] = ~(0ull | c.r[2].d[0]);\n    c.budget -= 4;\n"
                 "    { const bool t = c.r[2].d[0] != 0ull;  if (t) { goto L_00266684; } }")
edit(APPLY_PENDING_DAMAGE, FOE_SLOT_TEST, FOE_SLOT_TEST.replace("c.r[2].d[0] != 0ull;", "c.r[2].d[0] != 0ull && !%sboss_armour_vs_players(c.r[17].w[0]);" % AI))

# --- The game's difficulty step (ai_difficulty.cpp): GetProfileDifficulty is what global.lua picks the level's config script by. -------------------------------------------------------
ENTRY_HOOKS.append((0x0041D820, "if (%sprofile_difficulty(c)) return;" % AI))

# --- Rank sum (ai_objectives.cpp): Script_StatGetScore(handle) is what global.lua adds for player and player2. -----------------------------------------------------------------------------
ENTRY_HOOKS.append((0x00422630, "if (%sstat_score(c)) return;" % AI))
