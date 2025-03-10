/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright © 2022 Fragcolor Pte. Ltd. */

use super::Horizontal;
use crate::util;
use crate::HELP_OUTPUT_EQUAL_INPUT;
use crate::PARENTS_UI_NAME;
use shards::shard::Shard;
use shards::types::Context;
use shards::types::ExposedTypes;
use shards::types::InstanceData;
use shards::types::OptionalString;
use shards::types::ParamVar;
use shards::types::Parameters;
use shards::types::ShardsVar;
use shards::types::Type;
use shards::types::Types;
use shards::types::Var;
use shards::types::ANY_TYPES;
use shards::types::BOOL_TYPES;
use shards::types::SHARDS_OR_NONE_TYPES;

lazy_static! {
  static ref HORIZONTAL_PARAMETERS: Parameters = vec![
    (
      cstr!("Contents"),
      shccstr!("The UI contents."),
      &SHARDS_OR_NONE_TYPES[..],
    )
      .into(),
    (
      cstr!("Wrap"),
      shccstr!("Wrap the content once it reaches the right edge."),
      &BOOL_TYPES[..],
    )
      .into(),
    (
      cstr!("Centered"),
      shccstr!("Center the contents horizontally."),
      &BOOL_TYPES[..],
    )
      .into(),
  ];
}

impl Default for Horizontal {
  fn default() -> Self {
    let mut parents = ParamVar::default();
    parents.set_name(PARENTS_UI_NAME);
    Self {
      parents,
      requiring: Vec::new(),
      contents: ShardsVar::default(),
      wrap: false,
      centered: false,
      exposing: Vec::new(),
    }
  }
}

impl Shard for Horizontal {
  fn registerName() -> &'static str
  where
    Self: Sized,
  {
    cstr!("UI.Horizontal")
  }

  fn hash() -> u32
  where
    Self: Sized,
  {
    compile_time_crc32::crc32!("UI.Horizontal-rust-0x20200101")
  }

  fn name(&mut self) -> &str {
    "UI.Horizontal"
  }

  fn help(&mut self) -> OptionalString {
    OptionalString(shccstr!("Layout the contents horizontally."))
  }

  fn inputTypes(&mut self) -> &Types {
    &ANY_TYPES
  }

  fn inputHelp(&mut self) -> OptionalString {
    OptionalString(shccstr!(
      "The value that will be passed to the Contents shards of the layout."
    ))
  }

  fn outputTypes(&mut self) -> &Types {
    &ANY_TYPES
  }

  fn outputHelp(&mut self) -> OptionalString {
    *HELP_OUTPUT_EQUAL_INPUT
  }

  fn parameters(&mut self) -> Option<&Parameters> {
    Some(&HORIZONTAL_PARAMETERS)
  }

  fn setParam(&mut self, index: i32, value: &Var) -> Result<(), &str> {
    match index {
      0 => self.contents.set_param(value),
      1 => Ok(self.wrap = value.try_into()?),
      2 => Ok(self.centered = value.try_into()?),
      _ => Err("Invalid parameter index"),
    }
  }

  fn getParam(&mut self, index: i32) -> Var {
    match index {
      0 => self.contents.get_param(),
      1 => self.wrap.into(),
      2 => self.centered.into(),
      _ => Var::default(),
    }
  }

  fn requiredVariables(&mut self) -> Option<&ExposedTypes> {
    self.requiring.clear();

    // Add UI.Parents to the list of required variables
    util::require_parents(&mut self.requiring, &self.parents);

    Some(&self.requiring)
  }

  fn exposedVariables(&mut self) -> Option<&ExposedTypes> {
    self.exposing.clear();

    if util::expose_contents_variables(&mut self.exposing, &self.contents) {
      Some(&self.exposing)
    } else {
      None
    }
  }

  fn hasCompose() -> bool {
    true
  }

  fn compose(&mut self, data: &InstanceData) -> Result<Type, &str> {
    if !self.contents.is_empty() {
      self.contents.compose(data)?;
    }

    // Always passthrough the input
    Ok(data.inputType)
  }

  fn warmup(&mut self, ctx: &Context) -> Result<(), &str> {
    self.parents.warmup(ctx);
    if !self.contents.is_empty() {
      self.contents.warmup(ctx)?;
    }

    Ok(())
  }

  fn cleanup(&mut self) -> Result<(), &str> {
    if !self.contents.is_empty() {
      self.contents.cleanup();
    }
    self.parents.cleanup();

    Ok(())
  }

  fn activate(&mut self, context: &Context, input: &Var) -> Result<Var, &str> {
    if self.contents.is_empty() {
      return Ok(*input);
    }

    if let Some(ui) = util::get_current_parent(self.parents.get())? {
      if self.centered {
        ui.centered_and_justified(|ui| {
          if self.wrap {
            ui.horizontal_wrapped(|ui| {
              util::activate_ui_contents(context, input, ui, &mut self.parents, &mut self.contents)
            })
          } else {
            ui.horizontal(|ui| {
              util::activate_ui_contents(context, input, ui, &mut self.parents, &mut self.contents)
            })
          }
          .inner
        })
        .inner?;
      } else {
        if self.wrap {
          ui.horizontal_wrapped(|ui| {
            util::activate_ui_contents(context, input, ui, &mut self.parents, &mut self.contents)
          })
        } else {
          ui.horizontal(|ui| {
            util::activate_ui_contents(context, input, ui, &mut self.parents, &mut self.contents)
          })
        }
        .inner?;
      }

      // Always passthrough the input
      Ok(*input)
    } else {
      Err("No UI parent")
    }
  }
}
