import React from 'react';
import { ExpandMore } from '@mui/icons-material';
import {
  Typography,
  AccordionSummary,
  AccordionDetails,
  Accordion,
} from '@mui/material';
import { styled } from '@mui/material/styles';
import { useTranslation } from '@ts-libs/ui-i18n';

const Heading = styled(Typography)(({ theme }) => ({
  fontSize: theme.typography.pxToRem(20),
  //      fontWeight: theme.typography.fontWeightMedium,
}));

const Content = styled('div')({
  padding: 8,
  width: '100%',
});

interface IProps {
  title?: string;
  defaultExpanded?: boolean;
}

export function Expander({
  title,
  children,
  defaultExpanded,
}: React.PropsWithChildren<IProps>) {
  const { tr } = useTranslation();
  return (
    <Accordion defaultExpanded={defaultExpanded}>
      {title && (
        <AccordionSummary expandIcon={<ExpandMore />}>
          <Heading>{tr(title)}</Heading>
        </AccordionSummary>
      )}
      <AccordionDetails>
        <Content>{children}</Content>
      </AccordionDetails>
    </Accordion>
  );
}
