FROM emscripten/emsdk:3.1.74 AS wasm-build
WORKDIR /app
COPY plugin/Source/Engine ./plugin/Source/Engine
COPY plugin/Source/Dsp ./plugin/Source/Dsp
COPY wasm ./wasm
RUN emcmake cmake -S wasm -B build/wasm -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build/wasm --parallel

FROM node:22-alpine AS build
WORKDIR /app
COPY package*.json ./
RUN npm install
COPY --from=wasm-build /app/build/wasm/webplaysynth_engine.wasm /app/public/webplaysynth_engine.wasm
COPY . .
RUN npm run build

# The image Compose runs is the built application, not a bare Nginx image.
FROM nginx:1.27-alpine AS runtime
COPY nginx/default.conf /etc/nginx/conf.d/default.conf
COPY --from=build /app/dist/ /usr/share/nginx/html/
