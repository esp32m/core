export type Mutable<T> = { -readonly [P in keyof T]: T[P] };
export type TAsyncDisposer = () => Promise<void>;
export type TDisposer = () => void;

export type TJsonPrimitive = boolean | number | string | null;
export type TJsonArray = Array<TJsonValue> | ReadonlyArray<TJsonValue>;
export type TJsonObject = { [key: string]: TJsonValue };
export type TJsonValue = TJsonPrimitive | TJsonArray | TJsonObject;
