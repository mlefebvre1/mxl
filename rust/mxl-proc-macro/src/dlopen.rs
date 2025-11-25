// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use proc_macro2::TokenStream;
use syn::{ForeignItem, ForeignItemFn, Item, Visibility};

/// Generate the MxlApi struct based on the functions found in bindings.rs
/// It will have the form of
///
/// use mxl_sys::*;
///
/// pub struct MxlApi {
///     #[dlopen2_name = "mxlFunctionName"]
///     function_name: unsafe extern "C" fn(args) -> return_type,
/// }
pub fn generate_api(_input: TokenStream) -> TokenStream {
    let content =
        std::fs::read_to_string(mxl_sys::BINDINGS_PATH).expect("Failed to read bindings file");
    let functions = bindings_get_functions(&content);

    let mut api_fields = vec![];

    for func in functions {
        let func_name = &func.sig.ident;
        let func_inputs = &func.sig.inputs;
        let func_output = &func.sig.output;

        let attr_name =
            quote::format_ident! {"{}",convert_to_attribute_name(&func_name.to_string())};

        let dlopen2_name = func_name.to_string();

        let field = quote::quote! {
            #[dlopen2_name = #dlopen2_name]
            #attr_name: unsafe extern "C" fn(#func_inputs) #func_output,
        };
        api_fields.push(field);
    }

    let api_struct = quote::quote! {
        use mxl_sys::*;
        #[derive(dlopen2::wrapper::WrapperApi)]
        pub struct MxlApi {
            #(#api_fields)*
        }
    };

    api_struct
}

/// Extract all foreign functions from the bindings.rs file
pub fn bindings_get_functions(content: &str) -> Vec<ForeignItemFn> {
    let mut functions = vec![];

    let ast = syn::parse_file(content).expect("Failed to parse bindings file");
    for item in ast.items {
        if let Item::ForeignMod(extern_block) = item {
            for foreign_item in extern_block.items {
                if let ForeignItem::Fn(func) = foreign_item
                    && let Visibility::Public(_) = func.vis
                {
                    functions.push(func);
                }
            }
        }
    }
    functions
}

/// Convert the function name to the attribute name by removing the "mxl" prefix and changing
/// CamelCase to snake_case
fn convert_to_attribute_name(func_name: &str) -> String {
    to_snake_case(&func_name.replace("mxl", ""))
}

/// Convert a CamelCase string to snake_case
fn to_snake_case(s: &str) -> String {
    let mut out = String::new();

    for c in s.chars() {
        if c.is_uppercase() {
            if !out.is_empty() {
                out.push('_');
            }
            out.push(c.to_ascii_lowercase());
        } else {
            out.push(c);
        }
    }

    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_get_attribute_name() {
        assert_eq!(convert_to_attribute_name("mxlGetVersion"), "get_version");
    }

    #[test]
    fn test_bindings_get_functions() {
        let content = r#"
        fn dont_include_me();

        unsafe extern "C" {
            pub fn include_me();
            fn extern_but_not_pub();
        }
        extern "C" {
            pub fn include_me2();
             fn extern_but_not_pub2();
        }
        "#;

        let functions = bindings_get_functions(content);
        assert_eq!(functions.len(), 2);
        assert_eq!(functions[0].sig.ident, "include_me");
        assert_eq!(functions[1].sig.ident, "include_me2");
    }
}
