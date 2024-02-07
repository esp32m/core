import FingerprintJS, { GetResult } from '@fingerprintjs/fingerprintjs';
import { TBackendPluginUi } from '@ts-libs/backend';

let cachedPromise: Promise<GetResult | undefined>;

async function fingerprint() {
  try {
    const agent = await FingerprintJS.load();
    return agent.get();
  } catch (e) {
    console.warn(e);
  }
}

export const FingerprintPlugin: TBackendPluginUi = {
  name: 'browser-fingerprint',
  backend: {
    client: {
      info: async (info: Record<string, unknown>) => {
        if (!cachedPromise) cachedPromise = fingerprint();
        const r = await cachedPromise;
        if (r && info) {
          const { visitorId } = r;
          if (visitorId) Object.assign(info, { fingerprint: { visitorId } });
        }
      },
    },
  },
};
