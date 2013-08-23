if &cp | set nocp | endif
let s:cpo_save=&cpo
set cpo&vim
map! <xHome> <Home>
map! <xEnd> <End>
map! <S-xF4> <S-F4>
map! <S-xF3> <S-F3>
map! <S-xF2> <S-F2>
map! <S-xF1> <S-F1>
map! <xF4> <F4>
map! <xF3> <F3>
map! <xF2> <F2>
map! <xF1> <F1>
nnoremap \sa :call AddWordToDictionary()
nnoremap \sc :if &ft == 'html' | sy on | else | :sy clear SpellError | endif
nnoremap \sh :call HighlightSpellingErrors()
nnoremap \sl :w ! grep -v "^>" | grep -E -v "^[[:alpha:]-]+: " | ispell -l -d british | sort | uniq
nnoremap \si :w:!ispell -x -d british %:e
vnoremap p :let current_reg = @"gvdi=current_reg
map qS !Lrstar
map qs !%rstar
map qt yyPPItypedef df_A;kJ
map qp yyP^f(Byw^P DoEND
map qu mm/\*\/$"qdd:'m,.-1!uncom
map qc mm/END$"qdd:'m,.-1!com
nmap <F8> \sa
nmap <F10> \sc
nmap <F9> \sh
map <xHome> <Home>
map <xEnd> <End>
map <S-xF4> <S-F4>
map <S-xF3> <S-F3>
map <S-xF2> <S-F2>
map <S-xF1> <S-F1>
map <xF4> <F4>
map <xF3> <F3>
map <xF2> <F2>
map <xF1> <F1>
let &cpo=s:cpo_save
unlet s:cpo_save
set autoindent
set autowrite
set background=dark
set backspace=indent,eol,start
set cindent
set cinoptions=(0,g0,{s,>2s,n-s,^-s,t0
set equalprg=/bin/sort
set errorfile=o
set errorformat=%f:%l:%m
set expandtab
set formatoptions=
set history=100
set keywordprg=/usr/bin/man
set nomore
set path=.,/usr/include,~/evolution-core/src
set printoptions=paper:letter
set ruler
set runtimepath=~/.vim,/etc/vim,/usr/share/vim/vimfiles,/usr/share/vim/addons,/usr/share/vim/vim63,/usr/share/vim/vimfiles,/usr/share/vim/addons/after,~/.vim/after
set shiftwidth=2
set suffixes=.bak,~,.swp,.o,.info,.aux,.log,.dvi,.bbl,.blg,.brf,.cb,.ind,.idx,.ilg,.inx,.out,.toc
set viminfo='20,\"50
