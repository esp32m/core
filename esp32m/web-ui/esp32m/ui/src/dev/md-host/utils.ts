const ResetReasons = [
    'UNK1',
    'UNK2',
    'NRST',
    'POWR',
    'SOFT',
    'IWDG',
    'WWDG',
    'LPWR',
]

export function resetReasonToStr(rr: number) {
    let s = ''
    for (let i = 0; i < 8; i++)
        if (rr & (1 << i)) {
            if (s.length) s += ', ';
            s += ResetReasons[i];
        }
    return s;
}

export function serial2bytes(serial: number[]): Uint8Array {
    const buffer = new ArrayBuffer(serial.length * 4); // 4 bytes per uint32
    const view = new DataView(buffer);

    serial.forEach((num, index) => {
        view.setUint32(index * 4, num, true); // true = little endian
    });
    return new Uint8Array(buffer);
}
