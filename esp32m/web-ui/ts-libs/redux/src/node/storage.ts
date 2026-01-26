import { LocalStorage } from 'node-localstorage';

export class AsyncNodeStorage {
  private _localStorage: LocalStorage | null = null;
  private storageDirectoryPromise: Promise<string>;
  private maxStorageSize: number;

  constructor(storageDirectory: Promise<string>, maxStorageSize = 500) {
    this.storageDirectoryPromise = storageDirectory;
    this.maxStorageSize = maxStorageSize;
  }

  private async ensureStorage(): Promise<LocalStorage> {
    if (!this._localStorage) {
      const dir = await this.storageDirectoryPromise;
      this._localStorage = new LocalStorage(
        dir,
        this.maxStorageSize * 1024 * 1024
      );
    }
    return this._localStorage;
  }

  async getItem(key: string): Promise<string | null> {
    const storage = await this.ensureStorage();
    return storage.getItem(key);
  }

  async setItem(key: string, value: string | number): Promise<void> {
    const storage = await this.ensureStorage();
    storage.setItem(key, value + '');
  }

  async removeItem(key: string): Promise<void> {
    const storage = await this.ensureStorage();
    storage.removeItem(key);
  }

  async getAllKeys(): Promise<string[]> {
    const storage = await this.ensureStorage();
    const keys: string[] = [];
    for (let i = 0; i < storage.length; i++) {
      keys.push(storage.key(i));
    }
    return keys;
  }
}
