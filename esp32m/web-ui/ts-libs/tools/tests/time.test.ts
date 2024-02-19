import { TimeValue, sleep } from '../src';

test('math', async () => {
  for (let i = 0; i < 10; i++) {
    const hrstart = process.hrtime.bigint();
    const tvstart = TimeValue.fromBigint(hrstart);
    await sleep(Math.random() * 10);
    const hrend = process.hrtime.bigint();
    const tvend = TimeValue.fromBigint(hrend);
    const hrdiff = hrend - hrstart;
    const tvdiff = tvend.minus(tvstart);
    expect(tvend.compare(tvstart)).toEqual(1);
    expect(tvstart.compare(tvend)).toEqual(-1);
    expect(tvdiff.toBigint()).toEqual(hrdiff);
    expect(tvstart.plus(tvdiff).equals(tvend)).toEqual(true);
    const tvdiffNeg = tvstart.minus(tvend);
    expect(tvdiffNeg.toBigint()).toEqual(-hrdiff);
    expect(tvend.plus(tvdiffNeg).equals(tvstart)).toEqual(true);
    expect(tvdiff.compare(tvdiffNeg)).toEqual(1);
    expect(tvdiffNeg.compare(tvdiff)).toEqual(-1);
  }
});
