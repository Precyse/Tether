import { h, setText } from './dom.js';
import { createSteps } from './steps.js';

/**
 * A mod action's progress as one step row: the matrix spinner, the step text and the percent with a bar that tweens while it runs, the green check
 * at the end, the red cross with the reason on a failure and a Retry under it. A game the launcher builds also gets a Cancel while it builds and
 * an Open log beside Retry when the build failed. `variant` is 'hero' (over the art) or 'line' (in a settings row).
 */
export function createModRun({ send }, variant) {
  const steps = createSteps('small');
  const cancel = h('button', { class: 'btn btn-ghost mod-run-button', type: 'button', hidden: true });
  const log = h('button', { class: 'btn btn-ghost mod-run-button', type: 'button', hidden: true });
  const retry = h('button', { class: 'btn btn-ghost mod-run-button', type: 'button', hidden: true });
  const buttons = h('div', { class: 'mod-run-buttons' }, cancel, log, retry);
  const el = h('div', { class: `mod-run mod-run-${variant}`, hidden: true }, steps.el, buttons);
  let gameId = null;
  retry.addEventListener('click', () => send('mod.retry', { gameId }));
  cancel.addEventListener('click', () => send('mod.cancel', { gameId }));
  log.addEventListener('click', () => send('mod.openLog', { gameId }));

  function show(button, state) {
    button.hidden = !state;
    if (!state) return;
    setText(button, state.label);
    button.disabled = !state.enabled;
  }

  function update(run, id) {
    el.hidden = !run;
    steps.update(run ? [run.step] : []);
    if (!run) return;
    gameId = id;
    el.toggleAttribute('data-failed', run.failed);
    show(cancel, run.cancel);
    show(log, run.log);
    show(retry, run.retry);
    buttons.hidden = cancel.hidden && log.hidden && retry.hidden;
  }

  return { el, update };
}
