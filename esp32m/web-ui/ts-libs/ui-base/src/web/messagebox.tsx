import React from 'react';

import {
  Alert,
  Dialog,
  DialogTitle,
  DialogContent,
  DialogActions,
  Button,
  ButtonProps,
  LinearProgress,
} from '@mui/material';
import { useTranslation } from '@ts-libs/ui-i18n';

type HandlerResult = boolean | Promise<any>;

interface IAction {
  name: string;
  label?: string;
  onClick?: () => HandlerResult;
}

interface IPrompt {
  title?: string | React.ReactNode;
  content?: string | React.ReactNode;
  actions?: Array<IAction | string>;
  onClose?: (action?: IAction) => HandlerResult;
}

interface IProps {
  [key: string]: IPrompt;
}

interface IUseMessageBoxResult {
  messageBoxProps: IMBProps;
  open: (name: string) => void;
}

export const useMessageBox = (config: IProps): IUseMessageBoxResult => {
  const [open, setOpen] = React.useState<string | undefined>(undefined);
  const [alert, setAlert] = React.useState<string | undefined>(undefined);
  const [working, setWorking] = React.useState<boolean>(false);
  const { tr } = useTranslation();
  const prompt = open ? config[open] : undefined;
  const handleClose = (action?: IAction) => {
    let close: HandlerResult = true;
    if (action?.onClick) close = action.onClick();
    if (!close && prompt?.onClose) close = prompt.onClose(action);
    if (close instanceof Promise) {
      setWorking(true);
      close
        .then((r) => {
          if (r !== false) setOpen(undefined);
        })
        .catch((e) => setAlert(e + ''))
        .finally(() => setWorking(false));
    } else if (close !== false) setOpen(undefined);
  };
  const buttons =
    prompt?.actions &&
    prompt.actions.map((a) => {
      if (a instanceof React.Component) return a;
      if (typeof a === 'string') {
        const bp: ButtonProps = {
          onClick: () => handleClose({ name: a }),
          color: a == 'cancel' ? 'secondary' : 'primary',
          children: tr(a),
        };
        return <Button {...bp} key={a} />;
      }
      if (a && typeof a === 'object' && (a as IAction).name) {
        const { name, label, onClick: _, ...other } = a as IAction;
        const bp: ButtonProps = {
          onClick: () => handleClose(a),
          children: tr(label || name),
          ...other,
        };
        return <Button {...bp} key={name} />;
      }
    });
  const { title, content } = prompt || {};
  return {
    messageBoxProps: {
      title: tr(title),
      content: tr(content),
      working,
      alert,
      buttons,
      open: !!prompt,
      onClose: () => handleClose,
    },
    open: (name: string) => {
      setAlert(undefined);
      setWorking(false);
      setOpen(name);
    },
  };
};

interface IMBProps {
  title?: string | React.ReactNode;
  content?: string | React.ReactNode;
  working: boolean;
  alert: any;
  buttons: any;
  open: boolean;
  onClose: () => void;
}

export const MessageBox = ({
  title,
  content,
  working,
  alert,
  buttons,
  open,
  onClose,
}: IMBProps) => {
  const dp = { open, onClose };
  return (
    <Dialog {...dp}>
      {title && <DialogTitle>{title}</DialogTitle>}
      {working && <LinearProgress />}
      {alert && <Alert severity="error">{alert}</Alert>}
      {content && <DialogContent>{content}</DialogContent>}
      {buttons && buttons.length && <DialogActions>{buttons}</DialogActions>}
    </Dialog>
  );
};
