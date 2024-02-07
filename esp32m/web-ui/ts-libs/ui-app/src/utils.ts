import { useState, useEffect, createElement, ComponentType } from 'react';
import { createRoot } from 'react-dom/client';
import { Root } from './root';

export const formatError = (e: any) => {
  if (!e) return 'error';
  if (e.message) return e.message;
  if (e.name) return e.name;
  return e.toString();
};

export const useGeoPosition = () => {
  const [position, setPosition] = useState<GeolocationCoordinates>();
  const [error, setError] = useState<string>();
  useEffect(() => {
    const geo = navigator.geolocation;
    if (!geo) {
      setError('Geolocation is not supported');
      return;
    }
    const watcher = geo.watchPosition(
      ({ coords }) => {
        setPosition(coords);
      },
      (error) => {
        setError(error.message);
      }
    );
    return () => geo.clearWatch(watcher);
  }, []);
  return [position, error] as [
    GeolocationCoordinates | undefined,
    string | undefined,
  ];
};

export function renderRoot(root: ComponentType = Root): void {
  function init() {
    const app = document.getElementById('app');
    if (app) createRoot(app).render(createElement(root));
    else console.error('app container not found');
  }
  if (
    ['complete', 'loaded', 'interactive'].includes(document.readyState) &&
    document.body
  )
    init();
  else document.addEventListener('DOMContentLoaded', init, false);
}
