import React, { PropsWithChildren } from 'react';
import { diff } from 'deep-object-diff';
import { FormikConfig, FormikValues, useFormikContext } from 'formik';
import { Save } from '@mui/icons-material';
import { IconButton, Tooltip } from '@mui/material';
import { styled } from '@mui/material/styles';

import { useBackendApi } from '../backend';
import { MuiForm } from '@ts-libs/ui-forms';
import { CardBox } from '@ts-libs/ui-app';
import { useTranslation } from '@ts-libs/ui-i18n';

const StyledIconButton = styled(IconButton)({
  padding: 8,
});

interface TProps extends Partial<FormikConfig<any>> {
  name: string;
  title: string;
  initial: any;
}

interface TFormProps extends Partial<FormikConfig<any>> {
  name: string;
  initial: any;
}

export function ConfigForm(props: TFormProps) {
  const api = useBackendApi();
  const handleSubmit = async (values: FormikValues) => {
    const v = diff(props.initial, values);
    await api.setConfig(name, v);
  };
  const { name, children, ...other } = props;
  const fp = {
    onSubmit: handleSubmit,
    ...other,
  };
  return <MuiForm {...fp}>{children}</MuiForm>;
}

const InnerBox = ({
  title,
  children,
}: PropsWithChildren<{ title: string }>) => {
  const controller = useFormikContext();
  const { t } = useTranslation();
  return (
    <CardBox
      title={title}
      progress={controller.isSubmitting}
      action={
        controller.dirty ? (
          <Tooltip title={t('save changes')}>
            <StyledIconButton
              aria-label="save settings"
              onClick={controller.submitForm}
            >
              <Save />
            </StyledIconButton>
          </Tooltip>
        ) : null
      }
    >
      {children}
    </CardBox>
  );
};

export const ConfigBox = (props: React.PropsWithChildren<TProps>) => {
  const { title, children, ...other } = props;
  return (
    <ConfigForm {...other}>
      <InnerBox {...{ title, children }} />
    </ConfigForm>
  );
};
