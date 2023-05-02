export function binarySearch<T>(
  array: Array<T>,
  item: T,
  compare: (a: T, b: T) => number
): number {
  let low = 0;
  let high = array.length - 1;

  while (low <= high) {
    const mid = Math.floor((low + high) / 2);
    const cmp = compare(item, array[mid]);

    if (cmp > 0) low = mid + 1;
    else if (cmp < 0) high = mid - 1;
    else return mid; // item found
  }
  return -low - 1; // item not found, return the index to insert the item
}
