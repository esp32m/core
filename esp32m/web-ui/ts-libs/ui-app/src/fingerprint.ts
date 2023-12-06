import FingerprintJS, { GetResult } from '@fingerprintjs/fingerprintjs';
import { TBackendClientInfoPlugin } from '@ts-libs/backend';

let cachedPromise: Promise<GetResult | undefined>;

async function fingerprint() {
  try {
    const agent = await FingerprintJS.load();
    return agent.get();
  } catch (e) {
    console.warn(e);
  }
}

export const FingerprintPlugin: TBackendClientInfoPlugin = {
  name: 'browser-fingerprint',
  populateBackendClientInfo: (info: Record<string, unknown>) => {
    if (!cachedPromise) cachedPromise = fingerprint();
    return cachedPromise.then((r) => {
      if (r && info) {
        const { visitorId } = r;
        if (visitorId) Object.assign(info, { fingerprint: { visitorId } });
      }
    });
  },
};
