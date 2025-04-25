const table =
  'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=';

export function fromBase64(input: string): ArrayBuffer {
  const bytes = (input.length / 4) * 3;
  const ab = new ArrayBuffer(bytes);
  decode(input, ab);
  return ab;
}

function removePaddingChars(input: string) {
  const lkey = table.indexOf(input.charAt(input.length - 1));
  if (lkey == 64) return input.substring(0, input.length - 1);
  return input;
}

function decode(input: string, arrayBuffer: ArrayBuffer) {
  //get last chars to see if are valid
  input = removePaddingChars(input);
  input = removePaddingChars(input);

  const bytes = parseInt((input.length / 4) * 3 + '', 10);

  let uarray;
  let chr1, chr2, chr3;
  let enc1, enc2, enc3, enc4;
  let i = 0;
  let j = 0;

  if (arrayBuffer) uarray = new Uint8Array(arrayBuffer);
  else uarray = new Uint8Array(bytes);

  input = input.replace(/[^A-Za-z0-9+/=]/g, '');

  for (i = 0; i < bytes; i += 3) {
    //get the 3 octects in 4 ascii chars
    enc1 = table.indexOf(input.charAt(j++));
    enc2 = table.indexOf(input.charAt(j++));
    enc3 = table.indexOf(input.charAt(j++));
    enc4 = table.indexOf(input.charAt(j++));

    chr1 = (enc1 << 2) | (enc2 >> 4);
    chr2 = ((enc2 & 15) << 4) | (enc3 >> 2);
    chr3 = ((enc3 & 3) << 6) | enc4;

    uarray[i] = chr1;
    if (enc3 != 64) uarray[i + 1] = chr2;
    if (enc4 != 64) uarray[i + 2] = chr3;
  }

  return uarray;
}

export function toBase64(bytes: Uint8Array): string {
  let base64 = '';

  const byteLength = bytes.byteLength;
  const byteRemainder = byteLength % 3;
  const mainLength = byteLength - byteRemainder;

  let a;
  let b;
  let c;
  let d;
  let chunk;

  // Main loop deals with bytes in chunks of 3
  for (let i = 0; i < mainLength; i += 3) {
    // Combine the three bytes into a single integer
    chunk = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];

    // Use bitmasks to extract 6-bit segments from the triplet
    a = (chunk & 16515072) >> 18; // 16515072 = (2^6 - 1) << 18
    b = (chunk & 258048) >> 12; // 258048   = (2^6 - 1) << 12
    c = (chunk & 4032) >> 6; // 4032     = (2^6 - 1) << 6
    d = chunk & 63; // 63       = 2^6 - 1

    // Convert the raw binary segments to the appropriate ASCII encoding
    base64 += table[a] + table[b] + table[c] + table[d];
  }

  // Deal with the remaining bytes and padding
  if (byteRemainder === 1) {
    chunk = bytes[mainLength];

    a = (chunk & 252) >> 2; // 252 = (2^6 - 1) << 2

    // Set the 4 least significant bits to zero
    b = (chunk & 3) << 4; // 3   = 2^2 - 1

    base64 += `${table[a]}${table[b]}==`;
  } else if (byteRemainder === 2) {
    chunk = (bytes[mainLength] << 8) | bytes[mainLength + 1];

    a = (chunk & 64512) >> 10; // 64512 = (2^6 - 1) << 10
    b = (chunk & 1008) >> 4; // 1008  = (2^6 - 1) << 4

    // Set the 2 least significant bits to zero
    c = (chunk & 15) << 2; // 15    = 2^4 - 1

    base64 += `${table[a]}${table[b]}${table[c]}=`;
  }

  return base64;
}
