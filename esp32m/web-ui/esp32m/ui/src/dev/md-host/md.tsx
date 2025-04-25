import { useState } from "react";
import { useBackendApi, useModuleConfig, useModuleInfo, useModuleState } from "../../backend";
import { TDevInfo, TDevState } from "./types";
import { useSnackApi } from "@ts-libs/ui-snack";
import { useDispatch, useSelector } from "react-redux";
import { DialogForm, FieldText, useDialogForm } from "@ts-libs/ui-forms";
import * as Yup from 'yup';
import { ActionBliError, ActionBliStart } from "./state";
import { errorToString, isNumber } from "@ts-libs/tools";
import { MessageBox, useMessageBox } from "@ts-libs/ui-base";
import { resetReasonToStr, serial2bytes } from "./utils";
import { CardBox } from "@ts-libs/ui-app";
import { millisToStr } from "../../utils";
import { Button, Grid, IconButton } from "@mui/material";
import { Settings } from "@mui/icons-material";
import { NameValueList } from "../../app";
import { Motor } from "./motor";
import { crc32 } from "crc";
import { Buffer } from "buffer";

type TConfig = {
    addr?: number;
    ct?: number;
    m1?: { speed?: number };
    m2?: { speed?: number };
};

const validationSchema = Yup.object().shape({
    addr: Yup.number().min(4).max(0x7f),
    ct: Yup.number().min(3).max(64),
});

export const Dev = ({ name }: { name: string }) => {
    const [info] = useModuleInfo<TDevInfo>(name);
    const state = useModuleState<TDevState>(name);
    const { config, setConfig } = useModuleConfig<TConfig>(name);
    const api = useBackendApi();
    const [blInProgress, setBlInProgress] = useState(false);
    const snackApi = useSnackApi();
    const dispatch = useDispatch();
    const isStateSettnig = useSelector(api.module(name).selectors.isSettingState);
    const disabled = isStateSettnig || blInProgress;
    const [hook, openSettings] = useDialogForm({
        initialValues: config || {},
        onSubmit: async (v) => setConfig(v),
        validationSchema,
    });
    const enterBootloader = async () => {
        setBlInProgress(true);
        try {
            dispatch(ActionBliStart());
            await api.request(name, 'bootloader', {}, { timeout: 20000 });
        } catch (e) {
            dispatch(ActionBliError(errorToString(e)));
            snackApi.error(e);
        } finally {
            setBlInProgress(false);
        }
    };
    const { messageBoxProps, open } = useMessageBox({
        enterBootloader: {
            title:
                'This device will be put into bootloader mode to perform firmware update, proceed?',
            actions: [{ name: 'yes', onClick: () => { enterBootloader(); return true; } }, 'no'],
        },
    });

    if (!state || !info) return null;
    const { temperature, uptime, sys } = state;
    const list: Array<[string, any]> = [];
    const { serial, appver, clk, rr, ccrash, cwdt } = info;
    if (serial) {
        const uidBytes = serial2bytes(serial);
        list.push(['MCU UID', [...uidBytes].map(b => b.toString(16).padStart(2, '0')).join('')]);
        list.push(['Node ID', crc32(Buffer.from(uidBytes)).toString(16).padStart(8, '0')]);
    }
    if (appver)
        list.push(['Firmware version', `${appver[0]}.${appver[1]}`]);
    if (rr)
        list.push(['Reset reason', `${resetReasonToStr(rr)}`]);
    if (isNumber(clk))
        list.push(['Clock speed', `${clk.sys} MHz`]);
    if (ccrash || cwdt)
        list.push(['Crashed / WDT triggered times', `${ccrash || 0}/{}${cwdt || 0}`]);
    if (sys) {
        if (sys.cpu) list.push(['CPU load', `${sys.cpu} %`]);
        if (sys.fh) list.push(['Free heap / min heap', `${sys.fh}% / ${sys.mh}%`]);
        if (sys.st) list.push(['Min stack @ task number', `${sys.ms} words @ ${sys.st}`]);
        if (sys.tc) list.push(['Task count', `${sys.tc}`]);
    }
    if (isNumber(temperature)) list.push(['Temperature', `${temperature} °C`]);
    if (isNumber(uptime)) list.push(['Uptime', millisToStr(uptime * 1000)]);

    return <CardBox title={name} progress={disabled} action={
        <IconButton onClick={() => openSettings()}>
            <Settings />
        </IconButton>
    }>
        <NameValueList list={list} />
        <Motor name={name} num={1} state={state.m1} disabled={disabled} />
        <Motor name={name} num={2} state={state.m2} disabled={disabled} />
        <Grid container justifyContent="flex-end" spacing={2}>
            <Grid>
                <Button
                    onClick={() => open('enterBootloader')}
                    disabled={disabled}
                >
                    Enter bootloader
                </Button>
            </Grid>
        </Grid>
        <MessageBox {...messageBoxProps} />
        <DialogForm title="Configure motor driver" hook={hook}>
            <Grid container spacing={3}>
                <FieldText name="addr" label="I2C address" grid />
                <FieldText name="ct" label="Crash threshold" grid />
            </Grid>
        </DialogForm>
    </CardBox>
}
