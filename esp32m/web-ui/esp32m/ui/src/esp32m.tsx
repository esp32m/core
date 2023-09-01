import { TPlugin, findPlugin, register } from '@ts-libs/plugins';
import { Root } from '@ts-libs/ui-app';
import { Grid, Link, Typography } from '@mui/material';
import { Home } from '@mui/icons-material';
import { renderRoot } from '@ts-libs/ui-app';
import { VerticalMenuHeader, pluginUiNavbar } from '@ts-libs/ui-navbar';
import { pluginUiSnack } from '@ts-libs/ui-snack';
import { pluginUiBackend } from './backend';
import { SystemSummary } from './system';
import { ApInfoBox, MqttStateBox, StaInfoBox } from './net';
import { TContentPlugin, TUiThemePlugin } from '@ts-libs/ui-base';
import { pluginUi18n, Ti18nPlugin, useTranslation } from '@ts-libs/ui-i18n';
import { OtaPlugin } from './net/ota/plugin';
import { setDefine } from './utils';

const header = (
  <VerticalMenuHeader
    icon={
      <img
        style={{
          maxWidth: '100%',
          objectFit: 'cover',
          marginBottom: -10,
        }}
        src="data:image/jpeg;base64,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"
      ></img>
    }
  >
    <span style={{ marginLeft: 6 }}>ESP32 Manager</span>
  </VerticalMenuHeader>
);

const Footer = () => {
  const { t } = useTranslation();
  return (
    <Typography variant="caption">
      {t('Created with')} &nbsp;
      <Link href="https://esp32m.com/" target="blank">
        ESP32 Manager
      </Link>
    </Typography>
  );
};

const component = () => (
  <Grid container>
    <SystemSummary />
    <StaInfoBox />
    <ApInfoBox />
    <MqttStateBox />
  </Grid>
);

const plugin: TUiThemePlugin & Ti18nPlugin = {
  name: 'esp32m-defaults',
  ui: {
    theme: {
      config: {
        navbar: { header },
        footer: { content: Footer },
      },
    },
  },
  i18n: {
    resources: {
      en: {},
      de: {
        cancel: 'Abbrechen',
        'save changes': 'Änderungen speichern',
        Overview: 'Überblick',
        'Created with': 'Hergestellt mit',
      },
      uk: {
        'save changes': 'зберегти зміни',
        Overview: 'Огляд',
        'Created with': 'Створено за допомогою',
        cancel: 'Відміна',
      },
    },
  },
};

export type TEsp32mUiConfig = {
  readonly plugins?: Array<TPlugin>;
  readonly defines?: Record<string, any>;
};

export function startUi(config?: TEsp32mUiConfig) {
  register(
    ...[
      ...(config?.plugins || []),
      plugin,
      pluginUiBackend(),
      pluginUiSnack(),
      pluginUi18n(),
      pluginUiNavbar(),
      OtaPlugin,
    ]
  );
  if (config?.defines)
    for (const kv of Object.entries(config?.defines)) setDefine(kv[0], kv[1]);
  if (!findPlugin('home'))
    register({
      name: 'home',
      content: { icon: Home, title: 'Overview', component },
    } as TContentPlugin);
  renderRoot(Root);
}
