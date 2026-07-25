# Field GUI Handoff v0.1

The field subsystem remains Qt-free.

```text
typed field
    -> descriptor and const inspection
application-service adapter
    -> immutable field summary or preview
optional minimal Qt GUI
```

A future GUI may display field name, unit, value kind, location, entity count, selected patch, adapter-calculated min/max values and reduced immutable previews.

The GUI must not resize fields, own solver fields, mutate values directly or introduce Qt into `tsunami_fvm`.
