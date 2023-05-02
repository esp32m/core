import { IMultiProps } from './types';
import Relay from './Relay';
import { CardBox } from '@ts-libs/ui-app';

export default ({ nameOrList, title }: IMultiProps) => {
  const list = [];
  if (typeof nameOrList === 'string')
    list.push(<Relay name={nameOrList} title={title} />);
  else
    for (const i of nameOrList)
      if (typeof i === 'string')
        list.push(<Relay key={i} name={i} title={i} />);
      else list.push(<Relay key={i[0]} name={i[0]} title={i[1]} />);

  return <CardBox title={title || 'Relays'}>{list}</CardBox>;
};
