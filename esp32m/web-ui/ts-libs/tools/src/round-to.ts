const pows: Array<number> = [];

export function roundTo(decimal: number, fractionDigits: number) {
  if (pows.length == 0) for (let i = 0; i <= 20; i++) pows[i] = Math.pow(10, i);
  const d = pows[Math.min(Math.max(Math.round(fractionDigits), 0), 20)];
  return Math.round((decimal + Number.EPSILON) * d) / d;
}
