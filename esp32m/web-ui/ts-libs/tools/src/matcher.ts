import { isString } from './is-type';

/** credits to https://github.com/axtgr/wildcard-match/blob/master/src/index.ts */

function escapeRegExpChar(char: string) {
  if (
    char === '-' ||
    char === '^' ||
    char === '$' ||
    char === '+' ||
    char === '.' ||
    char === '(' ||
    char === ')' ||
    char === '|' ||
    char === '[' ||
    char === ']' ||
    char === '{' ||
    char === '}' ||
    char === '*' ||
    char === '?' ||
    char === '\\'
  ) {
    return `\\${char}`;
  } else {
    return char;
  }
}

function escapeRegExpString(str: string) {
  let result = '';
  for (let i = 0; i < str.length; i++) {
    result += escapeRegExpChar(str[i]);
  }
  return result;
}

function transform(
  pattern: string | string[],
  separator: string | boolean = true
): string {
  if (Array.isArray(pattern)) {
    const regExpPatterns = pattern.map((p) => `^${transform(p, separator)}$`);
    return `(?:${regExpPatterns.join('|')})`;
  }

  let separatorSplitter = '';
  let separatorMatcher = '';
  let wildcard = '.';

  if (separator === true) {
    separatorSplitter = '/';
    separatorMatcher = '[/\\\\]';
    wildcard = '[^/\\\\]';
  } else if (separator) {
    separatorSplitter = separator;
    separatorMatcher = escapeRegExpString(separatorSplitter);

    if (separatorMatcher.length > 1) {
      separatorMatcher = `(?:${separatorMatcher})`;
      wildcard = `((?!${separatorMatcher}).)`;
    } else {
      wildcard = `[^${separatorMatcher}]`;
    }
  }

  const requiredSeparator = separator ? `${separatorMatcher}+?` : '';
  const optionalSeparator = separator ? `${separatorMatcher}*?` : '';

  const segments = separator ? pattern.split(separatorSplitter) : [pattern];
  let result = '';

  for (let s = 0; s < segments.length; s++) {
    const segment = segments[s];
    const nextSegment = segments[s + 1];
    let currentSeparator = '';

    if (!segment && s > 0) {
      continue;
    }

    if (separator) {
      if (s === segments.length - 1) {
        currentSeparator = optionalSeparator;
      } else if (nextSegment !== '**') {
        currentSeparator = requiredSeparator;
      } else {
        currentSeparator = '';
      }
    }

    if (separator && segment === '**') {
      if (currentSeparator) {
        result += s === 0 ? '' : currentSeparator;
        result += `(?:${wildcard}*?${currentSeparator})*?`;
      }
      continue;
    }

    for (let c = 0; c < segment.length; c++) {
      const char = segment[c];

      if (char === '\\') {
        if (c < segment.length - 1) {
          result += escapeRegExpChar(segment[c + 1]);
          c++;
        }
      } else if (char === '?') {
        result += wildcard;
      } else if (char === '*') {
        result += `${wildcard}*?`;
      } else {
        result += escapeRegExpChar(char);
      }
    }

    result += currentSeparator;
  }

  return result;
}
const SerializedRegexp = /\/(.*?)\/([a-z]*)?$/i;

export function deserializeRegexp(serialized: string) {
  const fragments = serialized.match(SerializedRegexp);
  return fragments
    ? new RegExp(fragments[1], fragments[2] || '')
    : new RegExp(serialized);
}

export enum MatchType {
  Exact,
  Substring,
  Regex,
  Wildcard,
}

export class Matcher {
  readonly type: MatchType;
  constructor(
    readonly expr: string | RegExp,
    type?: MatchType
  ) {
    if (isString(expr)) {
      this._str = expr;
      if (!type) type = MatchType.Exact;
      else
        switch (type) {
          case MatchType.Regex:
            this._re = deserializeRegexp(expr);
            break;
          case MatchType.Wildcard:
            type = MatchType.Regex;
            this._re = new RegExp(`^${transform(expr)}$`);
        }
    } else if (expr instanceof RegExp) {
      this._re = expr;
      if (!type) type = MatchType.Regex;
    } else throw new TypeError('unsupported expression type: ' + typeof expr);
    this.type = type;
  }
  test(str: string) {
    if (isString(str))
      switch (this.type) {
        case MatchType.Exact:
          return this._str === str;
        case MatchType.Substring:
          return !!this._str && str.includes(this._str);
        case MatchType.Regex:
          return !!this._re && this._re?.test(str);
      }
    return false;
  }
  private readonly _re?: RegExp;
  private readonly _str?: string;
}
