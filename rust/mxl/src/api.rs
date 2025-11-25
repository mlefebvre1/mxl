// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::{path::Path, sync::Arc};

use dlopen2::wrapper::{Container, WrapperApi};

use crate::Result;
use mxl_proc_macro::mxl_dlopen2_api;

mxl_dlopen2_api!();

pub type MxlApiHandle = Arc<Container<MxlApi>>;

pub fn load_api(path_to_so_file: impl AsRef<Path>) -> Result<MxlApiHandle> {
    Ok(Arc::new(unsafe {
        Container::load(path_to_so_file.as_ref().as_os_str())
    }?))
}
