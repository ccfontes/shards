/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright © 2022 Fragcolor Pte. Ltd. */

use crate::shards::gui::EGUI_UI_SEQ_TYPE;
use crate::shards::gui::EGUI_UI_TYPE;
use crate::types::Context;
use crate::types::ExposedInfo;
use crate::types::ExposedTypes;
use crate::types::ParamVar;
use crate::types::Seq;
use crate::types::ShardsVar;
use crate::types::Var;
use crate::types::WireState;

pub(crate) fn activate_ui_contents<'a>(
  context: &Context,
  input: &Var,
  ui: &mut egui::Ui,
  parents: &mut ParamVar,
  contents: &mut ShardsVar,
) -> Result<Var, &'a str> {
  // pass the ui parent to the inner shards
  let var = parents.get();
  let mut seq: Seq = var.try_into()?;

  unsafe {
    let var = Var::new_object_from_ptr(ui as *const _, &EGUI_UI_TYPE);
    seq.push(var);
    parents.set(seq.as_ref().into());
  }

  let mut output = Var::default();
  let state = contents.activate(context, input, &mut output);

  // restore the previous ui parent
  seq.pop();
  parents.set(seq.as_ref().into());

  if state == WireState::Error {
    return Err("Failed to activate contents");
  }

  Ok(output)
}

pub(crate) fn expose_contents_variables(exposing: &mut ExposedTypes, contents: &ShardsVar) -> bool {
  if !contents.is_empty() {
    if let Some(contents_exposing) = contents.get_exposing() {
      for exp in contents_exposing {
        exposing.push(*exp);
      }
      true
    } else {
      false
    }
  } else {
    false
  }
}

pub(crate) fn get_current_parent<'a>(parents: Var) -> Result<Option<&'a mut egui::Ui>, &'a str> {
  let seq: Seq = parents.try_into()?;
  if !seq.is_empty() {
    Ok(Some(Var::from_object_ptr_mut_ref(
      seq[seq.len() - 1],
      &EGUI_UI_TYPE,
    )?))
  } else {
    Ok(None)
  }
}

pub(crate) fn require_parents(requiring: &mut ExposedTypes, parents: &ParamVar) {
  let exp_info = ExposedInfo {
    exposedType: EGUI_UI_SEQ_TYPE,
    name: parents.get_name(),
    help: cstr!("The parent UI objects.").into(),
    ..ExposedInfo::default()
  };
  requiring.push(exp_info);
}
