import { Table, TableRow, TableCell, TableBody } from '@mui/material';
import { useTranslation } from '@ts-libs/ui-i18n';

export type TNameValue = {
  name: any;
  value: any;
}

type TProps = {
  list: Array<TNameValue | Array<any>>;
}

export const NameValueList = ({ list }: TProps) => {
  const { t } = useTranslation();
  const rows = list.map((row, i) => {
    let name, value;
    if (Array.isArray(row)) {
      name = t(row[0]);
      value = row[1];
    } else {
      name = row.name;
      value = row.value;
    }
    return (
      <TableRow key={'row-' + i}>
        <TableCell
          component="th"
          scope="row"
          sx={{ paddingLeft: 0, paddingRight: 1 }}
        >
          {name}
        </TableCell>
        <TableCell align="right" sx={{ paddingLeft: 1, paddingRight: 0 }}>
          {value}
        </TableCell>
      </TableRow>
    );
  });
  return (
    <Table>
      <TableBody>{rows}</TableBody>
    </Table>
  );
};
