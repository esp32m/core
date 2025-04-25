import { Grid, Slider, ToggleButton, ToggleButtonGroup } from "@mui/material";
import { useModuleApi } from "../../backend";
import { TMotorState } from "./types";
import { SyntheticEvent } from "react";

export const Motor = ({ name, num, state, disabled }: { name: string, num: number, state?: TMotorState, disabled: boolean }) => {
    const { setState } = useModuleApi(name);
    if (!state) return null;
    const handleOp = (e: React.MouseEvent<HTMLElement>, op: number) => {
        if (op !== null) {
            setState({ [`m${num}`]: { op } });
        }
    }
    const handleSpeed = (_: SyntheticEvent | Event, speed: number) => {
        setState({ [`m${num}`]: { speed } });
    }
    return <Grid container spacing={2} >
        <ToggleButtonGroup
            color="primary"
            value={state.op}
            exclusive
            onChange={handleOp}
            disabled={disabled}
        >
            <ToggleButton value={0}>OFF</ToggleButton>
            <ToggleButton value={1}>FORWARD</ToggleButton>
            <ToggleButton value={2}>BACKWARD</ToggleButton>
            <ToggleButton value={4}>BRAKE</ToggleButton>
        </ToggleButtonGroup>
        <Slider value={state.speed} onChangeCommitted={handleSpeed} disabled={disabled} />
        {state.current}
    </Grid>
}
