use pest::Position;
use serde::{Deserialize, Serialize};

use crate::{RcBytesWrapper, RcStrWrapper};

#[derive(Parser)]
#[grammar = "shards.pest"]
pub struct ShardsParser;

#[derive(Serialize, Deserialize, Debug, Copy, Clone, Default)]
pub struct LineInfo {
  pub line: u32,
  pub column: u32,
}

#[derive(Debug)]
pub struct ShardsError {
  pub message: String,
  pub loc: LineInfo,
}

impl<'a> Into<ShardsError> for (&str, Position<'a>) {
  fn into(self) -> ShardsError {
    let (message, pos) = self;
    let line = pos.line_col().0;
    let column = pos.line_col().1;
    ShardsError {
      message: message.to_string(),
      loc: LineInfo {
        line: line as u32,
        column: column as u32,
      },
    }
  }
}

impl<'a> Into<ShardsError> for (String, Position<'a>) {
  fn into(self) -> ShardsError {
    let (message, pos) = self;
    let line = pos.line_col().0;
    let column = pos.line_col().1;
    ShardsError {
      message,
      loc: LineInfo {
        line: line as u32,
        column: column as u32,
      },
    }
  }
}

impl<'a> Into<ShardsError> for (&str, LineInfo) {
  fn into(self) -> ShardsError {
    let (message, pos) = self;
    ShardsError {
      message: message.to_string(),
      loc: pos,
    }
  }
}

impl<'a> Into<ShardsError> for (String, LineInfo) {
  fn into(self) -> ShardsError {
    let (message, pos) = self;
    ShardsError { message, loc: pos }
  }
}

impl<'a> Into<LineInfo> for Position<'a> {
  fn into(self) -> LineInfo {
    let line = self.line_col().0;
    let column = self.line_col().1;
    LineInfo {
      line: line as u32,
      column: column as u32,
    }
  }
}

impl Into<(u32, u32)> for LineInfo {
  fn into(self) -> (u32, u32) {
    (self.line, self.column)
  }
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub enum Number {
  Integer(i64),
  Float(f64),
  Hexadecimal(RcStrWrapper),
}

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq, Hash, Eq)]
pub struct Identifier {
  pub name: RcStrWrapper,
  pub namespaces: Vec<RcStrWrapper>,
}

impl Identifier {
  pub fn resolve(&self) -> RcStrWrapper {
    if self.namespaces.is_empty() {
      return self.name.clone();
    } else {
      // go thru all namespaces and concatenate them with "/" finally add name
      let mut result = String::new();
      for namespace in &self.namespaces {
        result.push_str(&namespace);
        result.push('/');
      }
      result.push_str(&self.name);
      result.into()
    }
  }
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub enum Value {
  None,
  Identifier(Identifier),
  Boolean(bool),
  Enum(RcStrWrapper, RcStrWrapper),
  Number(Number),
  String(RcStrWrapper),
  Bytes(RcBytesWrapper),
  Int2([i64; 2]),
  Int3([i32; 3]),
  Int4([i32; 4]),
  Int8([i16; 8]),
  Int16([i8; 16]),
  Float2([f64; 2]),
  Float3([f32; 3]),
  Float4([f32; 4]),
  Seq(Vec<Value>),
  Table(Vec<(Value, Value)>),
  Shard(Function),
  Shards(Sequence),
  EvalExpr(Sequence),
  Expr(Sequence),
  TakeTable(Identifier, Vec<RcStrWrapper>),
  TakeSeq(Identifier, Vec<u32>),
  Func(Function),
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct Param {
  pub name: Option<RcStrWrapper>,
  pub value: Value,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct Function {
  pub name: Identifier,
  pub params: Option<Vec<Param>>,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub enum BlockContent {
  Shard(Function),                          // Rule: Shard
  Shards(Sequence),                         // Rule: Shards
  Const(Value),                             // Rules: ConstValue, Vector
  TakeTable(Identifier, Vec<RcStrWrapper>), // Rule: TakeTable
  TakeSeq(Identifier, Vec<u32>),            // Rule: TakeSeq
  EvalExpr(Sequence),                       // Rule: EvalExpr
  Expr(Sequence),                           // Rule: Expr
  Func(Function),                           // Rule: BuiltIn
  Embed(Sequence), // This is a sequence that will include itself when evaluated
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct Block {
  pub content: BlockContent,
  pub line_info: Option<LineInfo>,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct Pipeline {
  pub blocks: Vec<Block>,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub enum Assignment {
  AssignRef(Pipeline, Identifier),
  AssignSet(Pipeline, Identifier),
  AssignUpd(Pipeline, Identifier),
  AssignPush(Pipeline, Identifier),
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub enum Statement {
  Assignment(Assignment),
  Pipeline(Pipeline),
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct Sequence {
  pub statements: Vec<Statement>,
}
