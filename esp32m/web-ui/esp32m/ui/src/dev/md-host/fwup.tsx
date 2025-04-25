import { Box, Button, Dialog, DialogContent, DialogTitle, LinearProgress, Step, StepContent, StepLabel, Stepper, Typography } from "@mui/material"
import { useState } from "react";
import { Name, TInfo } from "./types";
import { useBackendApi, useModuleInfo } from "../../backend";
import { useSnackApi } from "@ts-libs/ui-snack";
import { crc32 } from "crc";
import { Buffer } from "buffer";
import { toBase64 } from "../../base64";



export const FwUpdateButton = ({ uid }: { uid: number }) => {
    const [isStepperOpen, setStepperOpen] = useState(false);
    const [isWorking, setWorking] = useState(false);
    const [writeProgress, setWriteProgress] = useState(0);
    const [info] = useModuleInfo<TInfo>(Name);
    const [activeStep, setActiveStep] = useState(0);
    const snackApi = useSnackApi();
    const api = useBackendApi();
    const handleClose = () => { if (!isWorking) setStepperOpen(false); };
    const fwup = async (cmd: string, { chunk, size }: { chunk?: Buffer, size?: number }) => {
        const data = chunk && chunk.length ? toBase64(chunk) : undefined;
        await api.request(Name, 'fw-update', { cmd, uid, data, size });
    }
    const handleUpload = async (e: React.ChangeEvent<HTMLInputElement>) => {
        const file = e.target.files?.[0];
        if (!file) return;
        const unsuspend = api.statePoller.suspend();
        try {
            setWorking(true);
            const { maxSize, tailSize, tailMagic } = info?.app || { maxSize: 0, tailSize: 0, tailMagic: 0 };
            if (!maxSize || !tailSize || !tailMagic)
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
            const crc = crc32(bytes);
            const tail = Buffer.alloc(tailSize);
            tail.writeUInt32LE(tailMagic, 0);
            tail.writeUInt32LE(bytes.length, 4);
            tail.writeUInt32LE(crc, 8);
            const padding = Buffer.alloc(maxSize - bytes.length - tailSize, 0xff);
            const processed = Buffer.concat([bytes, padding, tail]);
            setActiveStep(2);
            await fwup('start', { size: processed.length });
            setWriteProgress(0);
            setActiveStep(3);
            let offset = 0
            const chunkSize = 64;
            while (offset < processed.length) {
                const chunk = processed.subarray(offset, offset + chunkSize);
                await fwup('write', { chunk });
                offset += chunk.length;
                setWriteProgress(Math.round((offset / processed.length) * 100));
            }
            setActiveStep(4);
            await fwup('finish', {});
            setActiveStep(5);
        } catch (e) {
            snackApi.error(e);
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
            <DialogContent dividers>

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