type t;
let create: unit => t;
let get: (t) => Ezjsonm.t;
let update: (t, string) => unit;
