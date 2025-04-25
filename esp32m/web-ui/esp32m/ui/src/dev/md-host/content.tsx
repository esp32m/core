import { useModuleState } from "../../backend";
import { Name, TState } from "./types";
import { Dev } from "./md";
import { Bl, BlSwitcher } from "./bl";

export const Content = () => {
    const state = useModuleState<TState>(Name);
    if (!state) return null;
    const { mds, bls } = state;
    const blse = Object.entries(bls || {});
    return <>
        {Array.isArray(mds) && mds.map((name, i) => <Dev key={i} name={name} />)}
        {!!blse?.length && blse.map(([uid, seen], i) => <Bl key={i} uid={parseInt(uid)} seen={seen} />)}
        <BlSwitcher />
    </>
}
