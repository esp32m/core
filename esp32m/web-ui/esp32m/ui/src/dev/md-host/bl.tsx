import { CardBox } from "@ts-libs/ui-app"
import { NameValueList } from "../../app"
import { millisToStr } from "../../utils"
import { Box, Button, Grid, Step, StepContent, StepLabel, Stepper, Typography } from "@mui/material";
import { Dialog, DialogContent, DialogTitle, IconButton } from "@mui/material";
import { Close as CloseIcon } from "@mui/icons-material";
import { useSelector } from "react-redux";
import { BliState, Name, TLocalState } from "./types";
import { useEffect, useState } from "react";
import { isNumber } from "@ts-libs/tools";
import { FwUpdateButton } from "./fwup";

export const Bl = ({ uid, seen }: { uid: number, seen: number }) => {
    return <CardBox title={`bootloader node ${uid.toString(16).padEnd(8, '0')}`}>
        <NameValueList list={[["Last seen", `${millisToStr(seen)} ago`]]} />
        <Grid container justifyContent="flex-end" spacing={2} sx={{ marginTop: 10 }}>
            <Grid>
                <FwUpdateButton uid={uid} />
            </Grid>
        </Grid>
    </CardBox>
}

const EnterBootloaderSteps = [
    {
        label: 'Getting ready to switch device to bootloader mode',
        description: `Finalizing pending operations and preparing to switch the device to bootloader mode.`,
    },
    {
        label: 'Requesting bootloader mode',
        description: `Sending a request to the device to enter bootloader mode by writing corresponding I2C register on the device.`,
    },
    {
        label: 'Powering off the device',
        description:
            'Powering off the device to reset its watchdog and prepare for bootloader mode.',
    },
    {
        label: 'Powering on the device',
        description: `Powering on the device, hopefully it will enter bootloader mode.`,
    },
    {
        label: 'Checking bootloader response',
        description: `Checking if the bootloader responds to UART commands. If it does, the device is in bootloader mode.`,
    },
    {
        label: 'Device in bootloader mode',
        description: `The device is in bootloader mode and is ready for firmware update.`,
    },
];

function EnterBootloaderStepper({ activeStep, error, handleContinue }: { activeStep: number; error?: string, handleContinue?: () => void }) {
    return (
        <Box sx={{ maxWidth: 400 }}>
            <Stepper activeStep={activeStep} orientation="vertical">
                {EnterBootloaderSteps.map((step, index) => (
                    <Step key={step.label}>
                        <StepLabel
                            optional={
                                !!error && (activeStep == index) ? (
                                    <Typography variant="caption" color="error">{error}</Typography>
                                ) : null
                            }
                            error={!!error && (activeStep == index)}
                        >
                            {step.label}
                        </StepLabel>
                        <StepContent>
                            <Typography>{step.description}</Typography>
                            {index === EnterBootloaderSteps.length - 1 && <Box sx={{ mb: 2 }}>
                                <Button
                                    onClick={handleContinue}
                                    sx={{ mt: 1, mr: 1 }}
                                >
                                    Continue
                                </Button>
                            </Box>}
                        </StepContent>
                    </Step>
                ))}
            </Stepper>
        </Box>
    );
}

export const BlSwitcher = () => {
    const ls = useSelector<any>((s) => s[Name]) as TLocalState;
    const [isStepperOpen, setStepperOpen] = useState(false);
    useEffect(() => {
        if (isNumber(ls?.bliState) && ls.bliState != BliState.Ready && !isStepperOpen) setStepperOpen(true);
    }, [ls?.bliState, isStepperOpen]);
    const handleClose = () => { setStepperOpen(false); };

    return <Dialog open={isStepperOpen} onClose={handleClose}>
        <DialogTitle>Bootloader entry progress</DialogTitle>
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
            <CloseIcon />
        </IconButton>
        <DialogContent dividers>
            <EnterBootloaderStepper activeStep={ls.bliState} error={ls.error} handleContinue={handleClose} />
        </DialogContent>
    </Dialog>

}