import '@xterm/xterm/css/xterm.css';

import { Terminal } from '@xterm/xterm';
import { Box, Button, Grid, MenuItem, TextField } from '@mui/material';
import { useCallback, useEffect, useMemo, useRef, useState } from 'react';

import { useModuleApi, useModuleConfig } from '../../backend';
import { Name, TConfig, TState } from './types';

export const Content = () => {
    const { config, setConfig, refreshConfig } = useModuleConfig<TConfig>(Name);
    const api = useModuleApi(Name);
    const apiRef = useRef(api);
    useEffect(() => {
        apiRef.current = api;
    }, [api]);

    const termHostRef = useRef<HTMLDivElement | null>(null);
    const termRef = useRef<Terminal | null>(null);
    const wsRef = useRef<WebSocket | null>(null);
    const decoderRef = useRef<TextDecoder | null>(null);

    const [connected, setConnected] = useState(false);
    const [baudrate, setBaudrate] = useState<number>(config?.baudrate || 115200);
    const [dataBits, setDataBits] = useState<number>(config?.dataBits || 3); // UART_DATA_8_BITS
    const [parity, setParity] = useState<number>(config?.parity || 0); // UART_PARITY_DISABLE
    const [stopBits, setStopBits] = useState<number>(config?.stopBits || 1); // UART_STOP_BITS_1

    useEffect(() => {
        if (!config || connected) return;
        if (config.baudrate) setBaudrate(config.baudrate);
        if (typeof config.dataBits === 'number') setDataBits(config.dataBits);
        if (typeof config.parity === 'number') setParity(config.parity);
        if (typeof config.stopBits === 'number') setStopBits(config.stopBits);
    }, [config, connected]);

    const wsUrl = useMemo(() => {
        const proto = window.location.protocol === 'https:' ? 'wss' : 'ws';
        return `${proto}://${window.location.host}/ws/uart`;
    }, []);

    const disconnect = useCallback(() => {
        try {
            wsRef.current?.close();
        } catch {
            // ignore
        }
        apiRef.current.request('disarm').catch(() => {
            // ignore
        });
    }, []);

    const connect = useCallback(async () => {
        if (wsRef.current && wsRef.current.readyState === WebSocket.OPEN) return;
        const term = termRef.current;
        if (!term) return;
        const state = (await apiRef.current.getState<TState>())?.data;
        // Check if UART is already in use (avoid confusing ws error on handshake).
        if (state?.active || (state?.uartOwner && state.uartOwner.length > 0)) {
            const owner = state?.uartOwner ? ` (${state.uartOwner})` : '';
            term.writeln(`\r\nAnother client is currently connected${owner}.`);
            return;
        }

        // Apply config changes before connecting.
        if (
            config &&
            (config.baudrate !== baudrate ||
                config.dataBits !== dataBits ||
                config.parity !== parity ||
                config.stopBits !== stopBits)
        ) {
            await setConfig({ baudrate, dataBits, parity, stopBits });
            await refreshConfig();
        }

        // Ensure the device allows the /ws/uart handshake.
        await apiRef.current.request('arm');

        term.writeln(`\r\n[connecting ${wsUrl}]`);
        const ws = new WebSocket(wsUrl);
        ws.binaryType = 'arraybuffer';
        wsRef.current = ws;

        ws.onopen = () => {
            setConnected(true);
            term.writeln('[connected]');
            // Reset decoder state for a new session.
            decoderRef.current = new TextDecoder('utf-8');
            // Move focus away from the Connect button into the terminal so Enter doesn't click Disconnect.
            setTimeout(() => {
                (document.activeElement as HTMLElement | null)?.blur?.();
                termRef.current?.focus();
            }, 0);
        };
        ws.onclose = () => {
            setConnected(false);
            if (wsRef.current === ws) wsRef.current = null;
            term.writeln('\r\n[disconnected]');
            apiRef.current.request('disarm').catch(() => {
                // ignore
            });
        };
        ws.onerror = () => {
            term.writeln('\r\n[ws error]');
        };
        ws.onmessage = (ev) => {
            if (!termRef.current) return;
            if (typeof ev.data === 'string') {
                termRef.current.write(ev.data);
                return;
            }
            const buf = ev.data as ArrayBuffer;
            const bytes = new Uint8Array(buf);
            const decoder = decoderRef.current;
            const text = decoder ? decoder.decode(bytes, { stream: true }) : new TextDecoder('utf-8').decode(bytes);
            termRef.current.write(text);
        };
    }, [baudrate, config, dataBits, parity, refreshConfig, setConfig, stopBits, wsUrl]);

    useEffect(() => {
        const host = termHostRef.current;
        if (!host) return;

        const encoder = new TextEncoder();
        decoderRef.current = new TextDecoder('utf-8');
        const term = new Terminal({
            convertEol: true,
            scrollback: 5000,
            fontFamily: 'monospace',
        });
        termRef.current = term;
        term.open(host);
        term.focus();
        term.writeln('UART terminal ready');

        const d = term.onData((data) => {
            const ws = wsRef.current;
            if (!ws || ws.readyState !== WebSocket.OPEN) return;
            ws.send(encoder.encode(data));
        });

        return () => {
            d.dispose();
            try {
                wsRef.current?.close();
            } catch {
                // ignore
            }
            term.dispose();
            termRef.current = null;
        };
    }, []);

    return (
        <Grid container spacing={2}>
            <Grid size={{ xs: 12 }} container spacing={1} alignItems="center">
                <Grid>
                    <TextField
                        size="small"
                        label="Baudrate"
                        value={baudrate}
                        onChange={(e) => setBaudrate(parseInt(e.target.value || '0', 10) || 0)}
                        disabled={connected}
                    />
                </Grid>
                <Grid>
                    <TextField
                        select
                        size="small"
                        label="Data bits"
                        value={dataBits}
                        onChange={(e) => setDataBits(parseInt(e.target.value as string, 10))}
                        disabled={connected}
                        sx={{ minWidth: 120 }}
                    >
                        <MenuItem value={0}>5</MenuItem>
                        <MenuItem value={1}>6</MenuItem>
                        <MenuItem value={2}>7</MenuItem>
                        <MenuItem value={3}>8</MenuItem>
                    </TextField>
                </Grid>
                <Grid>
                    <TextField
                        select
                        size="small"
                        label="Parity"
                        value={parity}
                        onChange={(e) => setParity(parseInt(e.target.value as string, 10))}
                        disabled={connected}
                        sx={{ minWidth: 120 }}
                    >
                        <MenuItem value={0}>None</MenuItem>
                        <MenuItem value={2}>Even</MenuItem>
                        <MenuItem value={3}>Odd</MenuItem>
                    </TextField>
                </Grid>
                <Grid>
                    <TextField
                        select
                        size="small"
                        label="Stop bits"
                        value={stopBits}
                        onChange={(e) => setStopBits(parseInt(e.target.value as string, 10))}
                        disabled={connected}
                        sx={{ minWidth: 120 }}
                    >
                        <MenuItem value={1}>1</MenuItem>
                        <MenuItem value={2}>1.5</MenuItem>
                        <MenuItem value={3}>2</MenuItem>
                    </TextField>
                </Grid>
                <Grid>
                    <Button
                        onClick={(e) => {
                            (e.currentTarget as HTMLButtonElement).blur();
                            return connected ? disconnect() : connect();
                        }}
                    >
                        {connected ? 'Disconnect' : 'Connect'}
                    </Button>
                </Grid>
                <Grid sx={{ marginLeft: 'auto' }}>
                    <Button
                        onClick={() => {
                            termRef.current?.clear();
                            termRef.current?.writeln('');
                        }}
                    >
                        Clear
                    </Button>
                </Grid>
            </Grid>

            <Grid size={{ xs: 12 }}>
                <Box
                    ref={termHostRef}
                    sx={{
                        width: '100%',
                        height: 420,
                        overflow: 'hidden',
                    }}
                />
            </Grid>
        </Grid>
    );
};
