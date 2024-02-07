import React from 'react';
import { Alert as MuiAlert, AlertColor } from '@mui/material';
import { errorToString } from '@ts-libs/tools';

interface IUseAlertResult {
  alertProps: IAlertProps;
  check: (t: any) => void;
}

interface IAlertProps {
  content?: string | React.ReactNode;
  severity: AlertColor;
}

export const Alert = ({ content, severity }: IAlertProps) =>
  content ? (
    <MuiAlert style={{ marginTop: 20 }} severity={severity}>
      {content}
    </MuiAlert>
  ) : (
    <div />
  );

export const useAlert = (): IUseAlertResult => {
  const [text, setText] = React.useState<string | undefined>(undefined);
  const [severity, setSeverity] = React.useState<AlertColor>('error');
  return {
    alertProps: { severity, content: text },
    check: (t: any, s?: AlertColor) => {
      if (t instanceof Promise) {
        t.then(() => setText(undefined)).catch((e: any) =>
          setText(errorToString(e))
        );
      } else if (t && t.error) setText(errorToString(t.error));
      else setText(undefined);
      if (s) setSeverity(s);
      return t;
    },
  };
};
