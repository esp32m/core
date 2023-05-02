function expectArray(v: unknown) {
  if (!Array.isArray(v)) throw new TypeError('array was expected');
}

function shuffleImpl<T>(array: T[]) {
  for (let i = array.length - 1; i > 0; i--) {
    const j = Math.floor(Math.random() * (i + 1));
    [array[i], array[j]] = [array[j], array[i]];
  }
}

export function shuffleSelf<T>(array: T[]) {
  expectArray(array);
  shuffleImpl(array);
}

export function shuffle<T>(array: T[]): T[] {
  expectArray(array);
  const target = [...array];
  shuffleImpl(target);
  return target;
}
