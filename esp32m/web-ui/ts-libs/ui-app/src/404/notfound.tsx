import { use404Elements } from './utils';

export const NotFound = () => {
  const elements = use404Elements();
  const element = elements[Math.floor(Math.random() * elements.length)];
  return element;
};
