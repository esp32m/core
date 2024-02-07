type TPathOptions = {
  readonly separator: string;
};

const defaultOptions: TPathOptions = {
  separator: '/',
};

export class Path implements TPathOptions {
  readonly segments: ReadonlyArray<string> = [];
  readonly separator!: string;
  readonly rooted: boolean = false;
  constructor(path: string | string[], options?: Partial<TPathOptions>) {
    Object.assign(this, defaultOptions, options);
    const { separator } = this;
    const segments = Array.isArray(path) ? path : path.split(separator);
    if (segments.length) {
      this.rooted = !segments[0];
      this.segments = segments.filter(Boolean);
    }
  }
  join(path: string | string[] | Path) {
    const other = path instanceof Path ? path : new Path(path);
    if (other.rooted) return other;
    const segments = [...this.segments, ...other.segments];
    if (this.rooted) segments.unshift('');
    const { separator } = this;
    return new Path(segments, { separator });
  }
  toString() {
    const segments = this.rooted ? ['', ...this.segments] : this.segments;
    return segments.join(this.separator);
  }
}

export function joinPath(...elements: string[]) {
  const nonEmpty = elements.filter(Boolean);
  const [first] = nonEmpty;
  const rooted = first?.startsWith('/');
  const segments = nonEmpty.flatMap((e) => e.split('/')).filter(Boolean);
  if (rooted) segments.unshift('');
  return segments.join('/');
}
