const MarkdownV2Special = '\\_*[]()~`>#+-=|{}.!/';
export function escapeMarkdownV2(s: string) {
  const a = new Array(s.length);
  for (let i = 0; i < s.length; i++) {
    const c = s.charAt(i);
    a[i] = MarkdownV2Special.includes(c) ? `\\${c}` : c;
  }
  return a.join('');
}
