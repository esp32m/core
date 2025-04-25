import { Grid, GridProps } from '@mui/material';
import { isBoolean } from '@ts-libs/tools';

type Props = {
  grid?: GridProps | boolean;
  children: React.ReactElement;
};

const Hoc = (props: Props) => {
  const { grid, children } = props;
  if (!grid) return children;
  const gp: GridProps = isBoolean(grid) ? { size: { xs: 'grow' } } : grid;
  return <Grid {...gp}>{children}</Grid>;
};

export default Hoc;
