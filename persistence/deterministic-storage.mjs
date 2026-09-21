export function createDeterministicStorage(initial = {slots: {A: null, B: null}}) {
  let state = structuredClone(initial);
  return Object.freeze({
    read() {
      return structuredClone(state);
    },
    replace(nextState) {
      state = structuredClone(nextState);
      return structuredClone(state);
    }
  });
}
