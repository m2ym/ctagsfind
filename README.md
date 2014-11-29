# ctagsfind

A command-line and Emacs frontend for Exuberant Ctags.

## Installation

Build `ctagsfind` executable.

```
$ make
```

Then, place `ctagsfind` executable to your `PATH` directory.

If you use Emacs, you also need to place `ctagsfind.el` to your
`load-path` directory, and put the following config into your
`.emacs`.

```Lisp
(when (require 'ctagsfind nil t)
  (global-set-key "\M-." 'ctagsfind-find-tag)
  (global-set-key "\M-*" 'ctagsfind-pop-tag))
```

## Usage

You can find tagged lines by executing the following command in a
(sub-)directory that has `tags` file.

```
$ ctagsfind QUERY
```

`M-.` (or `M-x ctagsfind-find-tag`) opens a location of matched tags.
`M-*` (or `M-x ctagsfind-pop-tag`) goes back to the previous location.
