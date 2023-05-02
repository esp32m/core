import { Button, Menu, MenuItem, capitalize } from '@mui/material';
import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { LanguageNames, getLanguages } from './utils';

export const I18nButton = () => {
  const [anchorEl, setAnchorEl] = useState<null | HTMLElement>(null);
  const languages = getLanguages();
  const { t, i18n } = useTranslation();
  const open = Boolean(anchorEl);
  const handleClick = (event: React.MouseEvent<HTMLElement>) => {
    setAnchorEl(event.currentTarget);
  };
  const handleClose = () => {
    setAnchorEl(null);
  };
  const handleSelect = (l: string) => {
    handleClose();
    i18n.changeLanguage(l);
  };
  return (
    <>
      <Button
        color="inherit"
        aria-owns={anchorEl?.id}
        aria-haspopup="true"
        onClick={handleClick}
      >
        {i18n.language || ''}
      </Button>
      <Menu
        id="auth-menu"
        anchorEl={anchorEl}
        open={open}
        onClose={handleClose}
        MenuListProps={{
          'aria-labelledby': 'auth-button',
        }}
      >
        {languages.map((l) => (
          <MenuItem key={l} onClick={() => handleSelect(l)}>
            {`${l.toUpperCase()} \u2014 ${capitalize(
              t(LanguageNames[l.toLowerCase()])
            )}`}
          </MenuItem>
        ))}
      </Menu>
    </>
  );
};
