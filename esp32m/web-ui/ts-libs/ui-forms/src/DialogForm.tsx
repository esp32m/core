import {
  Alert,
  Dialog,
  DialogActions,
  DialogContent,
  DialogProps,
  DialogTitle,
  LinearProgress,
} from '@mui/material';
import { errorToString } from '@ts-libs/tools';
import { ButtonBar, IButtonProps } from '@ts-libs/ui-base';
import { useTranslation } from '@ts-libs/ui-i18n';
import {
  Form,
  FormikConfig,
  FormikHelpers,
  FormikProps,
  FormikProvider,
  FormikValues,
  isFunction,
  useFormik,
} from 'formik';
import React, { useEffect, useState } from 'react';

interface IHookGeneratedProps {
  alert: any;
  open: boolean;
  onClose?: () => void;
  formikBag: FormikProps<FormikValues>;
}

export const useDialogForm = (
  props: FormikConfig<FormikValues>
): [IHookGeneratedProps, () => void] => {
  const [open, setOpen] = useState<boolean>(false);
  const [alert, setAlert] = useState<string | undefined>(undefined);
  const [enableReinitialize, setEnableReinitialize] = useState<boolean>(true);
  const onSubmit = (
    values: FormikValues,
    formikHelpers: FormikHelpers<FormikValues>
  ) => {
    return new Promise((resolve) => {
      try {
        const r = props.onSubmit(values, formikHelpers);
        if (r instanceof Promise)
          r.then((v) => {
            resolve(v);
            setOpen(false);
          }).catch((e) => {
            resolve(e); // we dont't reject, because formik is at a loss then
            setAlert(errorToString(e));
          });
        else resolve(values);
      } catch (e) {
        resolve(e);
        setAlert(errorToString(e));
      }
    });
  };
  const formikBag = useFormik({ ...props, onSubmit, enableReinitialize });
  useEffect(() => {
    if (props.initialValues && enableReinitialize) setEnableReinitialize(false);
  }, [props.initialValues]);
  return [
    {
      alert,
      open,
      onClose: () => setOpen(false),
      formikBag,
    },
    () => {
      setEnableReinitialize(true);
      setAlert(undefined);
      setOpen(true);
    },
  ];
};

interface IDialogFormProps extends Omit<Omit<DialogProps, 'open'>, 'children'> {
  hook: IHookGeneratedProps;
  buttons?: IButtonProps[] | undefined;
  children:
    | ((props: FormikProps<FormikValues>) => React.ReactNode)
    | React.ReactNode;
}

const defaultButtons = [
  { name: 'continue', submits: true },
  { name: 'cancel', cancels: true },
];

export const DialogForm = (props: IDialogFormProps) => {
  const { title, children, hook, buttons = defaultButtons, ...rest } = props;
  const { alert, open, onClose, formikBag } = hook;
  const dp = { open, onClose, ...rest };
  const bbp = { buttons, onSubmit: formikBag.submitForm, onCancel: onClose };
  const { t } = useTranslation();
  return (
    <Dialog {...dp}>
      {title && <DialogTitle>{t(title)}</DialogTitle>}
      {formikBag.isSubmitting && <LinearProgress />}
      {alert && <Alert severity="error">{alert}</Alert>}
      <FormikProvider value={formikBag}>
        <Form>
          <DialogContent>
            {isFunction(children) ? children(formikBag) : children}
          </DialogContent>
          <DialogActions>
            <ButtonBar {...bbp} />
          </DialogActions>
        </Form>
      </FormikProvider>
    </Dialog>
  );
};
