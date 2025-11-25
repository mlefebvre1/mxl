// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use proc_macro::TokenStream;
use syn::parse_macro_input;

mod dlopen;

#[proc_macro]
pub fn mxl_dlopen2_api(input: TokenStream) -> TokenStream {
    let input = parse_macro_input!(input);
    dlopen::generate_api(input).into()
}
