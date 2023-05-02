export interface IProps {
  name: string;
  title?: string;
}

export interface IState extends Array<number|Record<string, number>> {
 0: number,
 1: Record<string, number>,
}
