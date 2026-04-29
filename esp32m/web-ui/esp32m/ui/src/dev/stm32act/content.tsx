import { MessageBox, useMessageBox } from "@ts-libs/ui-base";
import { useModuleInfo, useModuleState, useModuleApi, useBackendApi, useModuleConfig } from "../../backend";
import { MotionEventKind, MotorState, Name, OpenState, TBootloaderInfo, TConfig, TInfo, TMotorInfo, TMotorState, TState, TWindowConfig } from "./types";
import { NameValueList } from "../../app";
import { CardBox } from "@ts-libs/ui-app";
import { formatBytes, millisToStr } from "../..";
import { enumTool } from "@ts-libs/tools";
import { useEffect, useMemo, useState } from "react";
import { FormControl, Grid, MenuItem, Select, Slider, Chip, Typography, Button, Dialog, DialogContent, Box, Stepper, Step, StepContent, StepLabel, LinearProgress, DialogTitle, IconButton, Stack, Divider } from "@mui/material";
import { Buffer } from "buffer";
import { toBase64 } from "../../base64";
import { useSnackApi } from "../../../../../ts-libs/ui-snack/src";
import { Close, Settings } from "@mui/icons-material";
import { DialogForm, FieldSwitch, FieldText, useDialogForm } from "@ts-libs/ui-forms";
import * as Yup from 'yup';

const telemetryChipSx = { height: 'auto', '& .MuiChip-label': { display: 'block', whiteSpace: 'normal', py: 0.5 } };

const validationSchema = Yup.object().shape({
    reg1: Yup.number().integer().min(0).max(255),
    reg2: Yup.number().integer().min(0).max(255),
    reg3: Yup.number().integer().min(0).max(255),
});


const Motor = ({ index, ms, mi, config, setConfig }: { index: number, ms: TMotorState, mi: TMotorInfo, config?: TWindowConfig, setConfig: (config: TWindowConfig) => Promise<void> }) => {
    const [, , current, fastCurrent, eventKind, eventDirection, elapsedMs, peakCurrent, confidence, , openState, position=0] = ms;
    const [state, setState] = useState(ms[0]);
    const [speed, setSpeed] = useState(ms[1]);
    const [disabled, setDisabled] = useState(false);
    const api = useModuleApi(Name);
    useEffect(() => {
        setState(ms[0]);
        setSpeed(ms[1]);
    }, [ms]);
    const [hook, openSettings] = useDialogForm({
        initialValues: config || {},
        onSubmit: (v) => setConfig(v as any),
        validationSchema,
    });
    const stateOptions = useMemo(() => enumTool(MotorState).toOptions().map(([value, name], i) =>
        <MenuItem value={value} key={i}>
            {`${name}`}
        </MenuItem>
    ), []);
    const handleSpeedChange = async (_: any, newValue: number | number[]) => {
        setSpeed(newValue as number);
        try {
            setDisabled(true);
            await api.request('setSpeed', [index, newValue]);
        } catch { } finally { setDisabled(false); }
    };
    const handleOperate = async (newState: MotorState) => {
        setState(newState);
        try {
            setDisabled(true);
            await api.request('operate', [index, newState]);
        } catch { } finally { setDisabled(false); }
    }
    const title = (mi.title || `Motor #${index + 1}`) + (openState !== OpenState.Unknown ? `: ${OpenState[openState]}` : '');
    return (<>
        <Stack>
            <Typography variant="subtitle2" style={{ marginTop: 6, marginLeft: 6 }}>{title}</Typography>
            <Grid container spacing={2}>
                <Grid size={{ xs: 4 }}>
                    <FormControl sx={{ m: 1, minWidth: 120 }} size="small" fullWidth>
                        <Select fullWidth disabled={disabled}
                            value={state}
                            onChange={(e) => {
                                handleOperate(e.target.value as MotorState);
                            }}
                        >
                            {stateOptions}
                        </Select>
                    </FormControl>
                </Grid>
                <Grid size={{ xs: 5 }} style={{ display: 'flex', alignItems: 'center' }}>
                    {!!mi.pwm &&
                        <Slider aria-label="Speed" value={speed} valueLabelDisplay="auto" defaultValue={100} disabled={disabled}
                            valueLabelFormat={(v) => `${v}%`} onChange={(_: Event, newValue: number) => { setSpeed(newValue); }} onChangeCommitted={handleSpeedChange} />
                    }
                    {Number.isInteger(position) && <LinearProgress variant="determinate" value={position} sx={{ width: '100%', mt: 1 }} />}
                </Grid>
                <Grid size={{ xs: 3 }} style={{ display: 'flex', alignItems: 'center', justifyContent: 'flex-end' }}>
                    <Chip label={`${current} mA`} variant="outlined" sx={telemetryChipSx} />
                    <IconButton onClick={() => openSettings()}>
                        <Settings />
                    </IconButton>
                </Grid>
            </Grid>
        </Stack>
        <Divider />
        <DialogForm title="Configure window" hook={hook}>
            <Grid container spacing={3}>
                <FieldSwitch name="terminalOpen" label="Terminal open" grid />
                <FieldSwitch name="terminalClose" label="Terminal close" grid />
            </Grid>
            <Grid container spacing={3}>
                <FieldText name="openTimeout" label="Open timeout, s" grid type="number" />
                <FieldText name="closeTimeout" label="Close timeout, s" grid type="number" />
            </Grid>
        </DialogForm>
    </>
    );
}

const Motors = ({ state }: { state: TState }) => {
    const [info] = useModuleInfo<TInfo>(Name);
    const api = useModuleApi(Name);
    const { config, setConfig } = useModuleConfig<TConfig>(Name);
    const [inProgress, setInProgress] = useState(false);
    const stm32req = async (name: string) => {
        setInProgress(true);
        try {
            await api.request(name, {}, { timeout: 20000 });
        } finally {
            setInProgress(false);
        }
    };
    const { messageBoxProps, open } = useMessageBox({
        stm32bootloader: {
            title:
                'This device will be put into bootloader mode to perform firmware update, proceed?',
            actions: [{ name: 'yes', onClick: () => { stm32req('bootloader'); return true; } }, 'no'],
        },
        stm32reset: {
            title:
                'This device will be reset, proceed?',
            actions: [{ name: 'yes', onClick: () => { stm32req('reset'); return true; } }, 'no'],
        },
    });

    const setWindowConfig = (index: number) => (windowConfig: Partial<TWindowConfig>) => {
        const windows = [];
        windows[index] = windowConfig
        return setConfig({ windows });
    }
    if (!info) return null;
    const list = [];
    if (Array.isArray(info.appver) && info.appver.length >= 2 && Array.isArray(info.uid))
        list.push(['Version / MCU UID', `${info.appver.join('.')} / ${info.uid.map(n => n.toString(16).padStart(2, '0')).join('')}`]);
    if (info.rf)
        list.push(['Startup flags', `${info.rf}`]);
    if (state.uptime !== undefined)
        list.push(['Uptime / ambient', `${millisToStr(state.uptime)} / ${state.ntc}°C`]);
    if (state.heap)
        list.push(['Heap total / free (min) ', `${formatBytes(state.heap.total)} / ${formatBytes(state.heap.free)} (${formatBytes(state.heap.minFree)})`]);
    if (state.cpuUsage !== undefined)
        list.push(['MCU Usage / Temp / Clock / VRef', `${state.cpuUsage}% / ${state.mcutemp}°C / ${(info.clk.sys / 1000000).toFixed(2)} MHz / ${(state.vrefint / 1000).toFixed(2)} V`]);
    if (state.current !== undefined)
        list.push(['Current', `${state.current} mA`]);
    if (!list.length) return null;
    const motors = [];
    if (state.motors)
        motors.push(...state.motors.map((ms, i) => {
            const mi = info.motors?.[i];
            if (!mi || mi.disabled)
                return null;
            return <Motor key={i} index={i} ms={ms} mi={mi} config={config?.windows?.[i]} setConfig={setWindowConfig(i)} />
        }).filter(m => !!m));
    return (
        <CardBox title={"STM32 Actuators"}>
            <NameValueList list={list} />
            {motors}
            <Grid container justifyContent="flex-end" spacing={2}>
                <Grid>
                    <Button
                        onClick={() => open('stm32reset')}
                        disabled={inProgress}
                    >
                        Reset
                    </Button>
                </Grid>
                <Grid>
                    <Button
                        onClick={() => open('stm32bootloader')}
                        disabled={inProgress}
                    >
                        Enter bootloader
                    </Button>
                </Grid>
            </Grid>
            <MessageBox {...messageBoxProps} />
        </CardBox>
    );
};

export const FwUpdateButton = () => {
    const [isStepperOpen, setStepperOpen] = useState(false);
    const [isWorking, setWorking] = useState(false);
    const [writeProgress, setWriteProgress] = useState(0);
    const [info] = useModuleInfo<TBootloaderInfo>(Name);
    const [activeStep, setActiveStep] = useState(0);
    const [error, setError] = useState<string>();
    const snackApi = useSnackApi();
    const api = useBackendApi();
    const handleClose = () => { if (isWorking) return; setStepperOpen(false); setError(undefined); setActiveStep(0); setWriteProgress(0); };
    const fwup = async (cmd: string, { chunk, size }: { chunk?: Buffer, size?: number }) => {
        const data = chunk && chunk.length ? toBase64(chunk) : undefined;
        await api.request(Name, 'fw-update', { cmd, data, size });
    }
    const handleUpload = async (e: React.ChangeEvent<HTMLInputElement>) => {
        const file = e.target.files?.[0];
        if (!file) return;
        const unsuspend = api.statePoller.suspend();
        try {
            setWorking(true);
            const { maxSize, tailSize, tailMagic, maxWriteSize } = info?.app || { maxSize: 0, tailSize: 0, tailMagic: 0, maxWriteSize: 0 };
            if (!maxSize || !tailSize || !tailMagic || !maxWriteSize)
                throw new Error("unexpected firmware info, please check the device firmware version.");
            if (file.size < 4096)
                throw new Error("File is too small, please select a valid firmware binary file.");
            if (file.size > maxSize - tailSize)
                throw new Error(`File is too big, please select a file smaller than ${maxSize} bytes.`);
            const bytes = Buffer.from(await file.arrayBuffer());
            if (bytes.byteLength != file.size)
                throw new Error("File size mismatch, please select a valid firmware binary file.");
            if (bytes.readUInt32LE(bytes.length - tailSize) == tailMagic)
                throw new Error("File has already been preprocessed, please select raw firmware binary file.");
            setActiveStep(1);
            setActiveStep(2);
            await fwup('start', { size: bytes.length });
            setWriteProgress(0);
            setActiveStep(3);
            let offset = 0
            const chunkSize = maxWriteSize;
            while (offset < bytes.length) {
                const chunk = bytes.subarray(offset, offset + chunkSize);
                await fwup('write', { chunk });
                offset += chunk.length;
                setWriteProgress(Math.round((offset / bytes.length) * 100));
            }
            setActiveStep(4);
            await fwup('finish', {});
            setActiveStep(5);
        } catch (e) {
            snackApi.error(e);
            setError(e.message);
        } finally {
            unsuspend();
            setWorking(false);
        }
    }
    return <>
        <Button
            onClick={() => setStepperOpen(true)}
            disabled={isStepperOpen}
        >
            Update firmware
        </Button>
        <Dialog open={isStepperOpen} onClose={handleClose}>
            <DialogTitle>Firmware update</DialogTitle>
            <IconButton
                aria-label="close"
                onClick={handleClose}
                sx={(theme) => ({
                    position: 'absolute',
                    right: 8,
                    top: 8,
                    color: theme.palette.grey[500],
                })}
            >
                <Close />
            </IconButton>
            <DialogContent dividers>
                {error && <Typography color="error">Error: {error}</Typography>}
                <Box sx={{ maxWidth: 400 }}>
                    <Stepper activeStep={activeStep} orientation="vertical">
                        <Step>
                            <StepLabel>
                                Select firmware binary
                            </StepLabel>
                            <StepContent>
                                <Box sx={{ mb: 2 }}>
                                    <input
                                        onChange={handleUpload}
                                        accept="application/octet-stream"
                                        style={{ display: 'none' }}
                                        id="fw-upload"
                                        type="file"
                                    />
                                    <label htmlFor="fw-upload">
                                        <Button variant="contained" component="span"
                                            sx={{ mt: 1, mr: 1 }}
                                        >
                                            Open file
                                        </Button>
                                    </label>
                                </Box>
                            </StepContent>
                        </Step>
                        <Step>
                            <StepLabel>
                                Processing firmware binary
                            </StepLabel>
                            <StepContent>
                                <Typography>Getting firmware image ready for upload</Typography>
                            </StepContent>
                        </Step>
                        <Step>
                            <StepLabel>
                                Erasing flash memory
                            </StepLabel>
                            <StepContent>
                                <Typography>Preparing flash for upload</Typography>
                            </StepContent>
                        </Step>

                        <Step>
                            <StepLabel>
                                Writing firmware to flash
                            </StepLabel>
                            <StepContent>
                                <Box sx={{ width: '100%' }}>
                                    <LinearProgress variant="determinate" value={writeProgress} />
                                </Box>
                            </StepContent>
                        </Step>

                        <Step>
                            <StepLabel>
                                Finalizing update
                            </StepLabel>
                            <StepContent>
                                Verifying firmware image and preparing for reboot
                            </StepContent>
                        </Step>


                        <Step>
                            <StepLabel>
                                Firmware update complete
                            </StepLabel>
                            <StepContent>
                                <Box sx={{ mb: 2 }}>
                                    <Button
                                        onClick={handleClose}
                                        sx={{ mt: 1, mr: 1 }}
                                    >
                                        Continue
                                    </Button>
                                </Box>
                            </StepContent>
                        </Step>

                    </Stepper>
                </Box>

            </DialogContent>
        </Dialog>
    </>

}

const Bootloader = () => {
    const [inProgress, setInProgress] = useState(false);
    const api = useModuleApi(Name);
    const stm32req = async (name: string) => {
        setInProgress(true);
        try {
            await api.request(name, {}, { timeout: 20000 });
        } finally {
            setInProgress(false);
        }
    };
    return <CardBox title={"STM32 Actuators bootloader"}>
        <Grid container justifyContent="flex-end" spacing={2}>
            <Grid>
                <Button
                    onClick={() => stm32req('stm32reset')}
                    disabled={inProgress}
                >
                    Boot
                </Button>
                <FwUpdateButton />
            </Grid>
        </Grid>

    </CardBox>;
}

export const Content = () => {
    const state = useModuleState<TState>(Name);
    if (!state) return null;
    switch (state.mode) {
        case 'bootloader':
            return <Bootloader />;
        case 'normal':
            return <Motors state={state} />;
        default:
            return null;
    }
}
