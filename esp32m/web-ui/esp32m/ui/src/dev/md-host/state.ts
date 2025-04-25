import { createAction, createReducer } from "@reduxjs/toolkit";
import { BliState, Name, TLocalState } from "./types";
import { actions } from "../../backend";

export const ActionBliStart = createAction(`${Name}/bli-start`);
export const ActionBliError = createAction<string | undefined>(`${Name}/bli-error`);


export const reducer = createReducer({} as TLocalState, (builder) => {
    builder
        .addCase(actions.response, (state, { payload: {source, name, data} }) => {
            if (source.startsWith('md-') && data)
                switch (name) {
                    case 'bootloader': {
                        state.bliState = data.state;
                        break;
                    }
                }
        })
        .addCase(ActionBliStart, (state, action) => {
            state.bliState = BliState.Preparing;
        }).addCase(ActionBliError, (state, action) => {
            state.error = action.payload;
        })
});
