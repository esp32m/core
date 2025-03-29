import { Encoding } from 'crypto';

export enum Endianness {
  BE = 'BE',
  LE = 'LE',
}

export type TStreamBufferInit = {
  capacity?: number;
  position?: number;
  endianness?: Endianness;
  data?: Uint8Array;
  autoGrow?: boolean;
};

export interface IStreamBuffer {
  endianness: Endianness;
  capacity: number;
  length: number;
  position: number;
  autoGrow: boolean;

  align(shift: number): IStreamBuffer;

  write(s: string, encoding?: BufferEncoding): IStreamBuffer;
  write(s: string, length: number, encoding?: BufferEncoding): IStreamBuffer;
  writeBytes(buf: Uint8Array): IStreamBuffer;

  writeBigUInt64(value: bigint): IStreamBuffer;
  writeBigUint64(value: bigint): IStreamBuffer;
  writeBigInt64(value: bigint): IStreamBuffer;
  writeUInt(value: number, byteLength: number): IStreamBuffer;
  writeUint(value: number, byteLength: number): IStreamBuffer;
  writeInt(value: number, byteLength: number): IStreamBuffer;
  writeUInt32(value: number): IStreamBuffer;
  writeUint32(value: number): IStreamBuffer;
  writeInt32(value: number): IStreamBuffer;
  writeUInt16(value: number): IStreamBuffer;
  writeUint16(value: number): IStreamBuffer;
  writeInt16(value: number): IStreamBuffer;
  writeUInt8(value: number): IStreamBuffer;
  writeUint8(value: number): IStreamBuffer;
  writeInt8(value: number): IStreamBuffer;
  writeFloat(value: number): IStreamBuffer;
  writeDouble(value: number): IStreamBuffer;

  readBytes(length: number): Uint8Array;
  readBigUInt64(): bigint;
  readBigUint64(): bigint;
  readBigInt64(): bigint;
  readUInt(byteLength: number): number;
  readUint(byteLength: number): number;
  readInt(byteLength: number): number;
  readUInt32(): number;
  readUint32(): number;
  readInt32(): number;
  readUInt16(): number;
  readUint16(): number;
  readInt16(): number;
  readUInt8(): number;
  readUint8(): number;
  readInt8(): number;
  readFloat(): number;
  readDouble(): number;

  toBytes(): Uint8Array;
}

let _isLe: boolean;

function isLe() {
  if (typeof _isLe === 'undefined') {
    const a32 = new Uint32Array([0x11223344]);
    const a8 = new Uint8Array(a32.buffer);
    if (a8[0] === 0x44) _isLe = true;
    else if (a8[0] === 0x11) _isLe = false;
    else throw new Error('could not detect system endianness');
  }
  return _isLe;
}

export type TReadStringOptions = {
  sizeBytes?: number;
  nullTerminated?: boolean;
  encoding?: Encoding;
  position?: number;
};

class StreamBuffer implements IStreamBuffer {
  autoGrow = false;
  constructor(options: TStreamBufferInit) {
    const { capacity, endianness, data, autoGrow, position } = options;
    if (data) {
      this._buffer = Buffer.from(data);
      this._length = this._buffer.length;
      this._position = position || 0;
    } else this._buffer = Buffer.alloc(capacity || 64);
    if (endianness) this.endianness = endianness;
    else this._le = isLe();
    if (typeof autoGrow === 'boolean') this.autoGrow = autoGrow;
  }
  get endianness() {
    return this._le ? Endianness.LE : Endianness.BE;
  }
  set endianness(e: Endianness) {
    switch (e) {
      case Endianness.BE:
        this._le = false;
        break;
      case Endianness.LE:
        this._le = true;
        break;
      default:
        throw new RangeError('invalid endianness: ' + e);
    }
  }
  get capacity() {
    return this._buffer.length;
  }
  set capacity(capacity: number) {
    if (capacity == this._buffer.length) return;
    if (capacity < 0) throw new RangeError('invalid capacity: ' + capacity);
    const newBuffer = Buffer.alloc(capacity);
    this._buffer.copy(newBuffer);
    this._buffer = newBuffer;
  }
  get length() {
    return this._length;
  }
  set length(length: number) {
    if (length < 0) throw new RangeError('invalid length: ' + length);
    const { capacity } = this;
    if (length > capacity)
      throw new RangeError(`length ${length} exceeds capaity ${capacity}`);
    this._length = length;
  }
  get position() {
    return this._position;
  }
  set position(position: number) {
    if (position < 0) throw new RangeError('invalid position: ' + position);
    const { length } = this;
    if (position > length)
      throw new RangeError(`position ${position} exceeds length ${length}`);
    this._position = position;
  }

  align(shift: number): IStreamBuffer {
    const inc = (1 << shift) - 1;
    const aligned = ((this._position + inc) >> shift) << shift;
    const diff = aligned - this._position;
    if (diff > 0) this.advance(diff);
    return this;
  }

  write(
    s: string,
    a1?: number | BufferEncoding,
    a2?: BufferEncoding
  ): IStreamBuffer {
    const encoding =
      typeof a2 === 'string' ? a2 : typeof a1 === 'string' ? a1 : undefined;
    let b = Buffer.from(s, encoding);
    if (typeof a1 === 'number') {
      if (b.length > a1) b = b.subarray(0, a1);
    }
    this.writeBytes(b);
    return this;
  }
  writeBytes(buf: Uint8Array): IStreamBuffer {
    this.ensureCapacity(buf.length);
    Buffer.from(buf).copy(this._buffer, this._position);
    this.advance(buf.length);
    return this;
  }

  writeBigUInt64(value: bigint): IStreamBuffer {
    this.ensureCapacity(8);
    this.advance(
      (this._le
        ? this._buffer.writeBigUInt64LE(value, this._position)
        : this._buffer.writeBigUInt64BE(value, this._position)) - this._position
    );
    return this;
  }
  writeBigUint64(value: bigint): IStreamBuffer {
    this.ensureCapacity(8);
    this.advance(
      (this._le
        ? this._buffer.writeBigUint64LE(value, this._position)
        : this._buffer.writeBigUint64BE(value, this._position)) - this._position
    );
    return this;
  }
  writeBigInt64(value: bigint): IStreamBuffer {
    this.ensureCapacity(8);
    this.advance(
      (this._le
        ? this._buffer.writeBigInt64LE(value, this._position)
        : this._buffer.writeBigInt64BE(value, this._position)) - this._position
    );
    return this;
  }
  writeUInt(value: number, byteLength: number): IStreamBuffer {
    this.ensureCapacity(byteLength);
    this.advance(
      (this._le
        ? this._buffer.writeUIntLE(value, this._position, byteLength)
        : this._buffer.writeUIntBE(value, this._position, byteLength)) -
        this._position
    );
    return this;
  }
  writeUint(value: number, byteLength: number): IStreamBuffer {
    this.ensureCapacity(byteLength);
    this.advance(
      (this._le
        ? this._buffer.writeUintLE(value, this._position, byteLength)
        : this._buffer.writeUintBE(value, this._position, byteLength)) -
        this._position
    );
    return this;
  }
  writeInt(value: number, byteLength: number): IStreamBuffer {
    this.ensureCapacity(byteLength);
    this.advance(
      (this._le
        ? this._buffer.writeIntLE(value, this._position, byteLength)
        : this._buffer.writeIntBE(value, this._position, byteLength)) -
        this._position
    );
    return this;
  }
  writeUInt32(value: number): IStreamBuffer {
    this.ensureCapacity(4);
    this.advance(
      (this._le
        ? this._buffer.writeUInt32LE(value, this._position)
        : this._buffer.writeUInt32BE(value, this._position)) - this._position
    );
    return this;
  }
  writeUint32(value: number): IStreamBuffer {
    this.ensureCapacity(4);
    this.advance(
      (this._le
        ? this._buffer.writeUint32LE(value, this._position)
        : this._buffer.writeUint32BE(value, this._position)) - this._position
    );
    return this;
  }
  writeInt32(value: number): IStreamBuffer {
    this.ensureCapacity(4);
    this.advance(
      (this._le
        ? this._buffer.writeInt32LE(value, this._position)
        : this._buffer.writeInt32BE(value, this._position)) - this._position
    );
    return this;
  }
  writeUInt16(value: number): IStreamBuffer {
    this.ensureCapacity(2);
    this.advance(
      (this._le
        ? this._buffer.writeUInt16LE(value, this._position)
        : this._buffer.writeUInt16BE(value, this._position)) - this._position
    );
    return this;
  }
  writeUint16(value: number): IStreamBuffer {
    this.ensureCapacity(2);
    this.advance(
      (this._le
        ? this._buffer.writeUint16LE(value, this._position)
        : this._buffer.writeUint16BE(value, this._position)) - this._position
    );
    return this;
  }
  writeInt16(value: number): IStreamBuffer {
    this.ensureCapacity(2);
    this.advance(
      (this._le
        ? this._buffer.writeInt16LE(value, this._position)
        : this._buffer.writeInt16BE(value, this._position)) - this._position
    );
    return this;
  }
  writeUInt8(value: number): IStreamBuffer {
    this.ensureCapacity(1);
    this.advance(
      this._buffer.writeUInt8(value, this._position) - this._position
    );
    return this;
  }
  writeUint8(value: number): IStreamBuffer {
    this.ensureCapacity(1);
    this.advance(
      this._buffer.writeUint8(value, this._position) - this._position
    );
    return this;
  }
  writeInt8(value: number): IStreamBuffer {
    this.ensureCapacity(1);
    this.advance(
      this._buffer.writeInt8(value, this._position) - this._position
    );
    return this;
  }
  writeFloat(value: number): IStreamBuffer {
    this.ensureCapacity(4);
    this.advance(
      (this._le
        ? this._buffer.writeFloatLE(value, this._position)
        : this._buffer.writeFloatBE(value, this._position)) - this._position
    );
    return this;
  }
  writeDouble(value: number): IStreamBuffer {
    this.ensureCapacity(8);
    this.advance(
      (this._le
        ? this._buffer.writeDoubleLE(value, this._position)
        : this._buffer.writeDoubleBE(value, this._position)) - this._position
    );
    return this;
  }

  readString(options: TReadStringOptions) {
    const remainingSize = this.length - this.position;
    const {
      sizeBytes = remainingSize,
      nullTerminated,
      encoding = 'utf8',
      position,
    } = options;
    if (typeof position == 'number') this.position = position;
    let rbs = Math.min(sizeBytes, remainingSize);
    let rb = this._buffer.subarray(this._position, rbs + this._position);
    if (nullTerminated) {
      const np = rb.indexOf(0);
      if (np >= 0) rb = rb.subarray(0, np);
      if (typeof options.sizeBytes !== 'number')
        rbs = rb.length + (np >= 0 ? 1 : 0);
    }
    this._position = this.ensureReadable(rbs);
    return rb.toString(encoding);
  }

  readBytes(length: number) {
    const newpos = this.ensureReadable(length);
    const bytes = this._buffer.subarray(this._position, newpos);
    this._position = newpos;
    return bytes;
  }
  readBigUInt64(): bigint {
    const newpos = this.ensureReadable(8);
    const result = this._le
      ? this._buffer.readBigUInt64LE(this._position)
      : this._buffer.readBigUInt64BE(this._position);
    this._position = newpos;
    return result;
  }
  readBigUint64(): bigint {
    const newpos = this.ensureReadable(8);
    const result = this._le
      ? this._buffer.readBigUint64LE(this._position)
      : this._buffer.readBigUint64BE(this._position);
    this._position = newpos;
    return result;
  }
  readBigInt64(): bigint {
    const newpos = this.ensureReadable(8);
    const result = this._le
      ? this._buffer.readBigInt64LE(this._position)
      : this._buffer.readBigInt64BE(this._position);
    this._position = newpos;
    return result;
  }
  readUInt(byteLength: number): number {
    const newpos = this.ensureReadable(byteLength);
    const result = this._le
      ? this._buffer.readUIntLE(this._position, byteLength)
      : this._buffer.readUIntBE(this._position, byteLength);
    this._position = newpos;
    return result;
  }
  readUint(byteLength: number): number {
    const newpos = this.ensureReadable(byteLength);
    const result = this._le
      ? this._buffer.readUintLE(this._position, byteLength)
      : this._buffer.readUintBE(this._position, byteLength);
    this._position = newpos;
    return result;
  }
  readInt(byteLength: number): number {
    const newpos = this.ensureReadable(byteLength);
    const result = this._le
      ? this._buffer.readIntLE(this._position, byteLength)
      : this._buffer.readIntBE(this._position, byteLength);
    this._position = newpos;
    return result;
  }
  readUInt32(): number {
    const newpos = this.ensureReadable(4);
    const result = this._le
      ? this._buffer.readUInt32LE(this._position)
      : this._buffer.readUInt32BE(this._position);
    this._position = newpos;
    return result;
  }
  readUint32(): number {
    const newpos = this.ensureReadable(4);
    const result = this._le
      ? this._buffer.readUint32LE(this._position)
      : this._buffer.readUint32BE(this._position);
    this._position = newpos;
    return result;
  }
  readInt32(): number {
    const newpos = this.ensureReadable(4);
    const result = this._le
      ? this._buffer.readInt32LE(this._position)
      : this._buffer.readInt32BE(this._position);
    this._position = newpos;
    return result;
  }
  readUInt16(): number {
    const newpos = this.ensureReadable(2);
    const result = this._le
      ? this._buffer.readUInt16LE(this._position)
      : this._buffer.readUInt16BE(this._position);
    this._position = newpos;
    return result;
  }
  readUint16(): number {
    const newpos = this.ensureReadable(2);
    const result = this._le
      ? this._buffer.readUint16LE(this._position)
      : this._buffer.readUint16BE(this._position);
    this._position = newpos;
    return result;
  }
  readInt16(): number {
    const newpos = this.ensureReadable(2);
    const result = this._le
      ? this._buffer.readInt16LE(this._position)
      : this._buffer.readInt16BE(this._position);
    this._position = newpos;
    return result;
  }
  readUInt8(): number {
    const newpos = this.ensureReadable(1);
    const result = this._buffer.readUInt8(this._position);
    this._position = newpos;
    return result;
  }
  readUint8(): number {
    const newpos = this.ensureReadable(1);
    const result = this._buffer.readUint8(this._position);
    this._position = newpos;
    return result;
  }
  readInt8(): number {
    const newpos = this.ensureReadable(1);
    const result = this._buffer.readInt8(this._position);
    this._position = newpos;
    return result;
  }
  readFloat(): number {
    const newpos = this.ensureReadable(4);
    const result = this._le
      ? this._buffer.readFloatLE(this._position)
      : this._buffer.readFloatBE(this._position);
    this._position = newpos;
    return result;
  }
  readDouble(): number {
    const newpos = this.ensureReadable(8);
    const result = this._le
      ? this._buffer.readDoubleLE(this._position)
      : this._buffer.readDoubleBE(this._position);
    this._position = newpos;
    return result;
  }
  toBytes() {
    return this._buffer.subarray(0, this._length);
  }
  private ensureCapacity(bytes: number) {
    const required = this._position + bytes;
    const capacity = this._buffer.length;
    if (capacity >= required) return;
    if (!this.autoGrow)
      throw new RangeError('buffer overflows and autoGrow is not enabled');
    this.capacity = (Math.max(required, capacity, 64) * 3) / 2;
  }
  private advance(bytes: number) {
    this._position += bytes;
    if (this._position > this._length) this._length = this._position;
  }
  private ensureReadable(length: number) {
    if (length < 0) throw new RangeError('invalid length ' + length);
    const newpos = this._position + length;
    if (newpos > this._length)
      throw new RangeError("can't read past end of buffer");
    return newpos;
  }
  private _buffer: Buffer;
  private _position = 0;
  private _length = 0;
  private _le = false;
}

export function newStreamBuffer(arg?: TStreamBufferInit | number | Uint8Array) {
  const options: TStreamBufferInit = {};
  if (typeof arg === 'number') options.capacity = arg;
  else if (arg)
    if (Buffer.isBuffer(arg) || arg instanceof Uint8Array) options.data = arg;
    else Object.assign(options, arg);
  return new StreamBuffer(options);
}
