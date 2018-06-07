export BOOTSTRAP="
[@input_stream [brl(input_stream) ! lambda!]@lambda lambda! |]@repl
ROOT<repl([bootstrap.edict] [r] file_open!) ARG0 decaps! <> encaps! RETURN @>
"
rlwrap -S "j2> " build/jj ${*:-"$BOOTSTRAP"}

