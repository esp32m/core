export const pick = <T = unknown>(obj: Record<string, T>, props: string[]) =>
  props.reduce((result, prop) => {
    result[prop] = obj[prop];
    return result;
  }, {} as Record<string, T>);
