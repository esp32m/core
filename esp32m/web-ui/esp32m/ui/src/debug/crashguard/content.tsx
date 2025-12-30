import { Checkpoint, Name, OtaRole, OtaState, TInfo, TOtaInfo, TOtaState, TRunState, TState } from './types';
import { useModuleInfo, useModuleState } from '../..';
import { NameValueList } from '../../app';
import { CardBox } from '@ts-libs/ui-app';
import { Tabs, Tab, Box, Link, Accordion, AccordionDetails, AccordionSummary, Divider, Typography, Pagination } from '@mui/material';
import { useState } from 'react';
import { ExpandMore } from '@mui/icons-material';
import { ResetReason } from '../../system/types';

interface TabPanelProps {
    children?: React.ReactNode;
    index: number;
    value: number;
}
function OtaTabPanel(props: TabPanelProps) {
    const { children, value, index, ...other } = props;

    return (
        <div
            role="tabpanel"
            hidden={value !== index}
            id={`simple-tabpanel-${index}`}
            aria-labelledby={`simple-tab-${index}`}
            {...other}
        >
            {value === index && <Box sx={{ p: 3 }}>{children}</Box>}
        </div>
    );
}


const otaMakeList = (info: TOtaInfo, state: TOtaState) => {
    const { app, otaState, role } = info || {};
    const { strike, crashes, brownouts, early, good, sig } = state || {};
    const list: any[] = [];
    if (crashes !== undefined || brownouts !== undefined || early !== undefined)
        list.push(["Panics / brownouts / early crashes", `${crashes || 0} / ${brownouts || 0} / ${early || 0}`]);
    if (Number.isInteger(good))
        list.push(["Best uptime", `${(good / 1000).toFixed(3)}s`]);
    if (sig)
        list.push(["Crash signature", sig]);
    if (Number.isInteger(role) || Number.isInteger(otaState) || Number.isInteger(strike))
        list.push(["Partition role / state / crash score", `${OtaRole[role] || 'Unknown'} / ${OtaState[otaState] || 'Unknown'} / ${strike}`]);
    if (app) {
        if (app.proj)
            list.push(["Project / version", `${app.proj} / ${app.ver}`]);
        if (app.date && app.time)
            list.push(["Build date / time", `${app.date} ${app.time}`]);
    }
    return list;
}

const runMakeList = (run: TRunState) => {
    const list = [];
    const { valid, boot, slot, rr, cp, inProgress, planned, unexpected, uptime, sig, ts } = run;
    if (valid !== undefined)
        list.push(["Valid run", valid ? "Yes" : "No"]);
    if (Number.isInteger(boot) || Number.isInteger(slot))
        list.push(["OTA partition / boot number", `${slot} / ${boot}`]);
    if (Number.isInteger(rr))
        list.push(["Reset reason", ResetReason[rr || 0]]);
    if (sig)
        list.push(["Crash signature", sig]);
    if (ts)
        list.push(["Start time", new Date(ts * 1000).toUTCString()]);
    if (Number.isInteger(uptime))
        list.push(["Uptime", `${(uptime / 1000).toFixed(3)}s`]);
    if (Number.isInteger(cp))
        list.push(["Checkpoint", `${Checkpoint[cp] || cp} (${cp})`]);
    const flags = [];
    if (inProgress)
        flags.push("in-progress");
    if (planned)
        flags.push(`planned-reset`);
    if (unexpected)
        flags.push("unexpected-reset");
    if (flags.length)
        list.push(["Run flags", flags.join(", ")]);
    return list;
}

const OtaTabs = ({ info, state }: { info: TInfo, state: TState }) => {
    const [activeTab, setActiveTab] = useState(0);
    const [expanded, setExpanded] = useState(false);
    const handleChange = (event: React.SyntheticEvent, newValue: number) => {
        setActiveTab(newValue);
    };

    const tabs = info.ota.map((ota, i) => {
        return <Tab label={ota.label || ("OTA" + i)} value={i} key={i} />
    });
    const panels = info.ota.map((ota, i) => {
        const list = otaMakeList(ota, state.ota[i]);
        return <OtaTabPanel value={activeTab} index={i} key={i}><NameValueList list={list} /></OtaTabPanel>
    });

    return <Accordion expanded={expanded} onChange={(_, isExpanded) => setExpanded(isExpanded)}>
        <AccordionSummary
            expandIcon={<ExpandMore />}
            id="ota-partitions-header"
        >
            {!expanded ? (
                <Typography>OTA partitions</Typography>
            ) : (
                <Box
                    onMouseDown={(e) => e.stopPropagation()}
                    onClick={(e) => e.stopPropagation()}
                    onKeyDown={(e) => {
                        // Prevent Enter/Space from toggling the accordion while
                        // navigating tabs.
                        if (e.key === 'Enter' || e.key === ' ') e.stopPropagation();
                    }}
                    sx={{ width: '100%' }}
                >
                    <Tabs onChange={handleChange} aria-label="OTA partitions">
                        {tabs}
                    </Tabs>
                </Box>
            )}
        </AccordionSummary>
        <AccordionDetails>
            <Divider />
            {panels}
        </AccordionDetails>
    </Accordion>;
}

const RunHistory = ({ runs }: { runs: TRunState[] }) => {
    const [activePage, setActivePage] = useState(1);
    const [expanded, setExpanded] = useState(false);
    const handleChange = (event: React.ChangeEvent<unknown>, page: number) => {
        setActivePage(page);
    };
    const list = runMakeList(runs[activePage - 1]);
    return <Accordion expanded={expanded} onChange={(_, isExpanded) => setExpanded(isExpanded)}>
        <AccordionSummary
            expandIcon={<ExpandMore />}
            id="run-history-header"
        >
            {!expanded ? (
                <Typography>Run history</Typography>
            ) : (
                <Box
                    onMouseDown={(e) => e.stopPropagation()}
                    onClick={(e) => e.stopPropagation()}
                    onKeyDown={(e) => {
                        // Prevent Enter/Space from toggling the accordion while
                        // navigating tabs.
                        if (e.key === 'Enter' || e.key === ' ') e.stopPropagation();
                    }}
                    sx={{ width: '100%' }}
                >
                    <Pagination onChange={handleChange} page={activePage} count={runs.length} />
                </Box>
            )}
        </AccordionSummary>
        <AccordionDetails>
            <Divider />
            <NameValueList list={list} />
        </AccordionDetails>
    </Accordion>;
};

const LastRunPanel = ({ run }: { run: TRunState }) => {
    const list = runMakeList(run);
    return <Accordion>
        <AccordionSummary
            expandIcon={<ExpandMore />}
            id="last-run-header"
        >
            <Typography>Last run details</Typography>
        </AccordionSummary>
        <AccordionDetails>
            <Divider />
            <NameValueList list={list} />
        </AccordionDetails>
    </Accordion>;
}

export const content = () => {
    const [info] = useModuleInfo<TInfo>(Name);
    const state = useModuleState<TState>(Name);
    if (!state || !info) return null;
    const { uptime, slot } = state;
    const { boot, lastSwitch, coredump } = info;
    const list = [];
    if (Number.isInteger(uptime))
        list.push(["Uptime", `${(uptime / 1000).toFixed(3)}s`]);
    if (Number.isInteger(boot) || Number.isInteger(slot))
        list.push(["Current OTA partition / boot number", `${slot} / ${boot}`]);
    if (lastSwitch)
        list.push(["Last partition switch occurred at boot #", `${lastSwitch}`]);
    if (coredump && coredump.uid)
        list.push(["Core dump uid / partition", <Link href="/debug/coredump.bin">{`${coredump.uid} / ${coredump.slot}`}</Link>]);
    return (
        <CardBox title={"Crash Guard"}>
            <NameValueList list={list} />
            <LastRunPanel run={info.last} />
            <RunHistory runs={info.history} />
            <OtaTabs info={info} state={state} />
        </CardBox>
    );
};
