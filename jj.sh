export BOOTSTRAP="
[@input_stream [brl(input_stream) ! lambda!]@lambda lambda! |]@repl
ROOT<repl([bootstrap.edict] [r] file_open!) RETURN ARG0 @>
"
rlwrap -S "j2> " build/jj ${*:-"$BOOTSTRAP"}
