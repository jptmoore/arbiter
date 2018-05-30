let init: unit => unit;

let info_f: (string, string) => Lwt.t(unit);

let debug_f: (string, string) => Lwt.t(unit);

let error_f: (string, string) => Lwt.t(unit);