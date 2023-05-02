export function copyOwnProps(
  dest: Record<string, any>,
  src: Record<string, any>,
  props: Array<string>
) {
  if (src && dest)
    props.forEach((p) => {
      if (src.hasOwnProperty(p)) dest[p] = src[p];
    });
}
