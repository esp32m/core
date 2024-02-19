import { Events, TEvent } from '../src';

enum EventTypes {
  Error = 'catchme',
}

interface EventObjects {
  [EventTypes.Error]: TEvent<EventTypes>;
}

describe('error handling', () => {
  const events = new Events<EventTypes, EventObjects>();
  test('throw', () => {
    events.subscribe(EventTypes.Error, () => {
      throw new Error('catch me if you can');
    });
    try {
      events.fire({ type: EventTypes.Error });
      fail('should have thrown');
    } catch {}
  });
});
