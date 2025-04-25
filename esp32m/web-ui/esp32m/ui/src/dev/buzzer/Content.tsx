import { useState } from 'react';
import {
  Button,
  ButtonProps,
  Divider,
  Grid,
  MenuItem,
  TextField,
} from '@mui/material';
import { styled } from '@mui/material/styles';
import { useBackendApi } from '../../backend';
import { CardBox } from '@ts-libs/ui-app';

const NoteFreq = [
  [16, 17, 18, 19, 21, 22, 23, 25, 26, 28, 29, 31],
  [33, 35, 37, 39, 41, 44, 46, 49, 52, 55, 58, 62],
  [65, 69, 73, 78, 82, 87, 92, 98, 104, 110, 117, 123],
  [131, 139, 147, 156, 165, 175, 185, 196, 208, 220, 233, 247],
  [262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494],
  [523, 554, 587, 622, 659, 698, 740, 784, 831, 880, 932, 988],
  [1047, 1109, 1175, 1245, 1319, 1397, 1480, 1568, 1661, 1760, 1865, 1976],
  [2093, 2218, 2349, 2489, 2637, 2794, 2960, 3136, 3322, 3520, 3729, 3951],
  [4186, 4435, 4699, 4978, 5274, 5588, 5920, 6272, 6645, 7040, 7459, 7902],
  [
    8372, 8870, 9397, 9956, 10548, 11175, 11840, 12544, 13290, 14080, 14917,
    15804,
  ],
  [
    16744, 17740, 18795, 19912, 21096, 22351, 23680, 25088, 26580, 28160, 29835,
    31609,
  ],
];
const NoteNames = [
  'C',
  'C#',
  'D',
  'D#',
  'E',
  'F',
  'F#',
  'G',
  'G#',
  'A',
  'A#',
  'H',
];
const OctaveNames = [
  'sub-contra',
  'contra',
  'great',
  'small',
  'one-line',
  'two-line',
  'three-line',
  'four-line',
  'five-line',
  'six-line',
  'seven-line',
];
  const NoteButton = styled(Button)({ minWidth: 30, padding: 0, marginTop: 6, marginBottom: 6 });
  const StyledDivider = styled(Divider)({ marginBottom: 12, marginTop: 12 });

const toMenuItem = (e: string, i: number) => (
  <MenuItem key={i} value={i}>
    {e}
  </MenuItem>
);
const HappyBirthday = [
  264, 250, 0, 500, 264, 250, 0, 250, 297, 1000, 0, 250, 264, 1000, 0, 250, 352,
  1000, 0, 250, 330, 2000, 0, 500, 264, 250, 0, 500, 264, 250, 0, 250, 297,
  1000, 0, 250, 264, 1000, 0, 250, 396, 1000, 0, 250, 352, 2000, 0, 500, 264,
  250, 0, 500, 264, 250, 0, 250, 264, 1000, 0, 250, 440, 1000, 0, 250, 352, 500,
  0, 250, 352, 250, 0, 250, 330, 1000, 0, 250, 297, 2000, 0, 500, 466, 250, 0,
  500, 466, 250, 0, 250, 440, 1000, 0, 250, 352, 1000, 0, 250, 396, 1000, 0,
  250, 352, 2000, 0, 250,
];
const content = (props: { name: string; title?: string }) => {
  const [octave, setOctave] = useState(5);
  const [duration, setDuration] = useState(200);
  const [playing, setPlaying] = useState(false);
  const [music, setMusic] = useState(JSON.stringify(HappyBirthday));
  const [error, setError] = useState('');
  const { name, title } = props;
  const api=useBackendApi()
  const play = (data: any) => {
    setPlaying(true);
    api.request(name, 'play', data).finally(() => setPlaying(false));
  };
  const playMusic = () => {
    try {
      play(JSON.parse(music));
      setError('');
    } catch (e:any) {
      setError(e.message || e);
    }
  };
  const playNote = (i: number) => {
    const freq = NoteFreq[octave][i];
    play([freq, duration]);
  };
  const toNoteButton = (e: string, i: number) => {
    const bp: ButtonProps = {
      variant: 'contained',
      color: 'secondary',
      disabled: playing,
      onClick: () => playNote(i),
    };
    return (
      <Grid size={{ xs: 1 }} key={i}>
        <NoteButton {...bp}>{e}</NoteButton>
      </Grid>
    );
  };
  return (
    <CardBox title={title || 'Buzzer'}>
      <Grid container spacing={3}>
        <Grid size={{ xs: 9 }}>
          <TextField
            variant="standard"
            label="Octave"
            value={octave}
            onChange={(e) => setOctave(Number(e.target.value))}
            select
            fullWidth
          >
            {OctaveNames.map(toMenuItem)}
          </TextField>
        </Grid>
        <Grid size={{ xs: 3 }}>
          <TextField
            variant="standard"
            label="Duration, ms"
            value={duration}
            onChange={(e) => setDuration(Number(e.target.value))}
          />
        </Grid>
      </Grid>
      <Grid container spacing={1}>
        {NoteNames.map(toNoteButton)}
      </Grid>
      <StyledDivider/>
      <Grid container spacing={3} alignItems="flex-end">
        <Grid size={{ xs: 10 }}>
          <TextField
            variant="standard"
            label="Music as [frequency,duration...]"
            value={music}
            helperText={error}
            onChange={(e) => setMusic(e.target.value)}
            fullWidth
          />
        </Grid>
        <Grid size={{ xs: 2 }}>
          <Button
            variant="contained"
            color="primary"
            disabled={playing}
            onClick={() => playMusic()}
          >
            Play
          </Button>
        </Grid>
      </Grid>
    </CardBox>
  );
};

export default content;
