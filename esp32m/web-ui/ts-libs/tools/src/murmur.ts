import { isString } from './is-type';

export function toBytes(str: string | Uint8Array): Uint8Array {
  if (isString(str)) {
    if (TextEncoder) return new TextEncoder().encode(str);
    if (Buffer) return Buffer.from(str);
  }
  if (str instanceof Uint8Array) return str;
  throw new Error('string or Uint8Array expected');
}

export function murmur2(str: string | Uint8Array, seed = 0): number {
  const m = 0x5bd1e995;
  const data = toBytes(str);
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  let len = data.length;
  let h = seed ^ len;
  let i = 0;

  while (len >= 4) {
    let k = view.getUint32(i);
    i += 4;

    k = (k & 0xffff) * m + ((((k >>> 16) * m) & 0xffff) << 16);
    k ^= k >>> 24;
    k = (k & 0xffff) * m + ((((k >>> 16) * m) & 0xffff) << 16);

    h = ((h & 0xffff) * m + ((((h >>> 16) * m) & 0xffff) << 16)) ^ k;

    len -= 4;
  }

  switch (len) {
    case 3:
      h ^= (data[i + 2] & 0xff) << 16;
    case 2:
      h ^= (data[i + 1] & 0xff) << 8;
    case 1:
      h ^= data[i] & 0xff;
      h = (h & 0xffff) * m + ((((h >>> 16) * m) & 0xffff) << 16);
  }

  h ^= h >>> 13;
  h = (h & 0xffff) * m + ((((h >>> 16) * m) & 0xffff) << 16);
  h ^= h >>> 15;

  return h >>> 0;
}

const C1 = 0xcc9e2d51;
const C2 = 0x1b873593;
const R1 = 15;
const R2 = 13;
const M = 5;
const N = 0xe6546b64;

/** Gives `m` left rotated as a 32-bit int */
const rotateLeft = (m: number, n: number): number =>
  (m << n) | (m >>> (32 - n));

/** Unsigned 32-bit Murmur Hash 3 with the given uint 32-bit `seed` */
export function murmur3(str: string | Uint8Array, seed = 0) {
  const bytes = toBytes(str);
  let hash = seed >>> 0;

  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.length);

  for (let i = 0; i < Math.floor(bytes.length / 4); i += 4) {
    let k = view.getUint32(i);

    k = Math.imul(k, C1);
    k = rotateLeft(k, R1);
    k = Math.imul(k, C2);

    hash ^= k;
    hash = rotateLeft(hash, R2);
    hash = Math.imul(hash, M) + N;
  }

  const remainingBytes = bytes.length % 4;
  const i = bytes.length - remainingBytes;
  let k = 0;
  switch (remainingBytes) {
    case 3:
      k |= bytes[i + 2] << 16;
    // deno-lint-ignore no-fallthrough
    case 2:
      k |= bytes[i + 1] << 8;
    case 1:
      k |= bytes[i];

      k = Math.imul(k, C1);
      k = rotateLeft(k, R1);
      k = Math.imul(k, C2);

      hash ^= k;
  }

  hash ^= bytes.length;

  hash ^= hash >>> 16;
  hash = Math.imul(hash, 0x85ebca6b);
  hash ^= hash >>> 13;
  hash = Math.imul(hash, 0xc2b2ae35);
  hash ^= hash >>> 16;

  return hash >>> 0;
}
