create table lecturas (
  id bigint generated always as identity primary key,
  aula text not null,
  eco2_ppm int,
  tvoc_ppb int,
  temp_c numeric(4,1),
  humedad_pct numeric(4,1),
  estado text check (estado in ('verde', 'amarillo', 'rojo')),
  ocupado boolean,
  fecha_hora timestamptz default now()
);

alter table lecturas enable row level security;
create policy "insertar_anon" on lecturas for insert to anon with check (true);
create policy "leer_anon" on lecturas for select to anon using (true);
