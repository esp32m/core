import { LocalStorage } from 'node-localstorage';

export class AsyncNodeStorage {
  private localStorage: LocalStorage;

  constructor(storageDirectory: string, maxStorageSize = 500) {
    this.localStorage = new LocalStorage(
      storageDirectory,
      maxStorageSize * 1024 * 1024
    );
  }

  getItem(key: string) {
    return new Promise<string | null>((resolve, reject) => {
      try {
        const storedValue = this.localStorage.getItem(key);
        process.nextTick(() => resolve(storedValue));
      } catch (e) {
        reject(e);
      }
    });
  }

  setItem(key: string, value: string | number) {
    return new Promise<void>((resolve, reject) => {
      try {
        this.localStorage.setItem(key, value + '');
        process.nextTick(() => resolve());
      } catch (e) {
        reject(e);
      }
    });
  }

  removeItem(key: string) {
    return new Promise<void>((resolve, reject) => {
      try {
        this.localStorage.removeItem(key);
        process.nextTick(() => resolve());
      } catch (e) {
        reject(e);
      }
    });
  }

  getAllKeys() {
    return new Promise<string[]>((resolve, reject) => {
      try {
        const keys: string[] = [];
        for (let i = 0; i < this.localStorage.length; i++) {
          keys.push(this.localStorage.key(i));
        }
        process.nextTick(() => resolve(keys));
      } catch (e) {
        reject(e);
      }
    });
  }
}
