pushd build
rlwrap -S "e> " ./jj '[@input_stream [brl(input_stream) ! lambda!]@lambda lambda! |]@repl ROOT<repl([../bootstrap.edict] [r] file_open!)> [RETURN] ARG0 @'
popd
